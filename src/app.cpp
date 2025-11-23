#include "app.hpp"

#include "keyboard_movement_controller.hpp"
#include "simple_render_system.hpp"
#include "point_light_system.hpp"
#include "shadow_render_system.hpp"
#include "skybox_render_system.hpp"
#include "camera.hpp"
#include "buffer.hpp"
#include "ros_bridge.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define STB_IMAGE_IMPLEMENTATION
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "stb/stb_image.h"

#include <array>
#include <cassert>
#include <stdexcept>
#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>


namespace cvsim {

    SimApp::SimApp() { 
        globalPool =
            DescriptorPool::Builder(device)
            .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * 2)
            .build();

        createShadowResources();
        createSkyboxCubemap();

        loadSimObjects();
    }

    SimApp::~SimApp() {}

    void SimApp::destroyShadowResources() {
        if (shadowSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device.device(), shadowSampler, nullptr);
            shadowSampler = VK_NULL_HANDLE;
        }
        if (shadowFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.device(), shadowFramebuffer, nullptr);
            shadowFramebuffer = VK_NULL_HANDLE;
        }
        if (shadowRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.device(), shadowRenderPass, nullptr);
            shadowRenderPass = VK_NULL_HANDLE;
        }
        if (shadowImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device.device(), shadowImageView, nullptr);
            shadowImageView = VK_NULL_HANDLE;
        }
        if (shadowImage != VK_NULL_HANDLE) {
            vkDestroyImage(device.device(), shadowImage, nullptr);
            shadowImage = VK_NULL_HANDLE;
        }
        if (shadowImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device.device(), shadowImageMemory, nullptr);
            shadowImageMemory = VK_NULL_HANDLE;
        }
    }

    void SimApp::destroySkyboxCubemap() {
        if (skyboxSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device.device(),   skyboxSampler, nullptr);
            skyboxSampler = VK_NULL_HANDLE;
        }
        if (skyboxImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device.device(), skyboxImageView, nullptr);
            skyboxImageView = VK_NULL_HANDLE;
        }
        if (skyboxImage != VK_NULL_HANDLE) {
            vkDestroyImage(device.device(), skyboxImage, nullptr);
            skyboxImage = VK_NULL_HANDLE;
        }
        if (skyboxImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device.device(), skyboxImageMemory, nullptr);
            skyboxImageMemory = VK_NULL_HANDLE;
        }
    }

    void SimApp::run() {
        std::vector<std::unique_ptr<Buffer>> uboBuffers(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < uboBuffers.size(); i++) {
            uboBuffers[i] = std::make_unique<Buffer>(
                device,
                sizeof(GlobalUbo),
                1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            uboBuffers[i]->map();
        }
        
        auto globalSetLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        std::vector<VkDescriptorSet> globalDescriptorSets(SwapChain::MAX_FRAMES_IN_FLIGHT);

        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        shadowImageInfo.imageView   = shadowImageView;
        shadowImageInfo.sampler     = shadowSampler;

        VkDescriptorImageInfo skyboxImageInfo{};
        skyboxImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        skyboxImageInfo.imageView = skyboxImageView;
        skyboxImageInfo.sampler = skyboxSampler;

        for (int i = 0; i < globalDescriptorSets.size(); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();

            DescriptorWriter(*globalSetLayout, *globalPool)
                .writeBuffer(0, &bufferInfo)      // binding = 0 (UBO)
                .writeImage(1, &shadowImageInfo)  // binding = 1 (shadowMap sampler2D)
                .writeImage(2, &skyboxImageInfo)
                .build(globalDescriptorSets[i]);
        }

        SimpleRenderSystem simpleRenderSystem{
            device,
            renderer.getSwapChainRenderPass(),
            globalSetLayout->getDescriptorSetLayout() };
            
        ShadowRenderSystem shadowRenderSystem{
            device,
            shadowRenderPass,
            globalSetLayout->getDescriptorSetLayout() };
        PointLightSystem pointLightSystem{
           device,
           renderer.getSwapChainRenderPass(),
           globalSetLayout->getDescriptorSetLayout() };

        SkyboxRenderSystem skyboxRenderSystem(
            device,
            renderer.getSwapChainRenderPass(),
            globalSetLayout->getDescriptorSetLayout()
        );

        Camera camera{};
        auto viewerObject = SimObject::createSimObject();
        viewerObject.transform.translation.z = -2.5f;
        KeyboardMovementController cameraController{};

        auto currentTime = std::chrono::high_resolution_clock::now();

        //
        struct FrameCapture { VkBuffer buf{}; VkDeviceMemory mem{}; void* mapped{}; size_t size{}; };
        std::array<FrameCapture, enginev::SwapChain::MAX_FRAMES_IN_FLIGHT> captures;
        
        RosImageBridge ros;

        auto extent = renderer.getSwapChainExtent();
        auto recreateCaptures = [&]() {
            for (auto &c : captures) {
                if (c.mapped) {
                    vkUnmapMemory(device.device(), c.mem);
                    c.mapped = nullptr;
                }
                if (c.buf) {
                    vkDestroyBuffer(device.device(), c.buf, nullptr);
                    c.buf = VK_NULL_HANDLE;
                }
                if (c.mem) {
                    vkFreeMemory(device.device(), c.mem, nullptr);
                    c.mem = VK_NULL_HANDLE;
                }
            }

            extent = renderer.getSwapChainExtent();
            const size_t byteSize = extent.width * extent.height * 4; 
            
            for (size_t i = 0; i < captures.size(); ++i) {
                VkBuffer buf; VkDeviceMemory mem;
                device.createBuffer(
                    byteSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    buf, mem);
                void* ptr = nullptr;
                vkMapMemory(device.device(), mem, 0, VK_WHOLE_SIZE, 0, &ptr);
                captures[i] = FrameCapture{ buf, mem, ptr, byteSize };
            }
        };

        recreateCaptures();

        //
        while (!window.shouldClose()) {
            glfwPollEvents();
            
            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime =
                std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;
            //управление с клавиатуры
            cameraController.moveInPlaneXZ(window.getGLFWwindow(), frameTime, viewerObject);
            
            // управление с помощью ROS
            auto cmd = ros.getLastCmd();
            float yawSpeed = 1.0f;
            float pitchSpeed = 1.0f;

            viewerObject.transform.rotation.y += yawSpeed * cmd.angular.z * frameTime;
            viewerObject.transform.rotation.x += pitchSpeed * cmd.angular.y * frameTime;

            viewerObject.transform.rotation.x = 
                glm::clamp(viewerObject.transform.rotation.x, -1.5f, 1.5f);

            float yaw = viewerObject.transform.rotation.y;
            const glm::vec3 forwardDir{ sin(yaw), 0.f, cos(yaw)};
            const glm::vec3 rightDir{ forwardDir.z, 0.f, -forwardDir.x};
            const glm::vec3 upDir{ 0.f, -1.f, 0.f};

            float lx = static_cast<float>(cmd.linear.x);
            float ly = static_cast<float>(cmd.linear.y);
            float lz = static_cast<float>(cmd.linear.z);

            glm::vec3 moveDir =
                forwardDir * lx +
                rightDir * ly +
                upDir * lz;
            
            float rosMoveSpeed = 1.0f;

            if (glm::length(moveDir) > std::numeric_limits<float>::epsilon()) {
                viewerObject.transform.translation +=
                    rosMoveSpeed * frameTime * moveDir;
            }

            camera.setViewYXZ(viewerObject.transform.translation, viewerObject.transform.rotation);
           
            float aspect = renderer.getAspectRatio();

            camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.f); // íàñòðîéêà âèäèìîñòè

            if (auto commandBuffer = renderer.beginFrame()) {
                int frameIndex = renderer.getFrameIndex();

                VkExtent2D newExtent = renderer.getSwapChainExtent();

                if (newExtent.width != extent.width ||
                newExtent.height != extent.height) {
                    vkDeviceWaitIdle(device.device());
                    extent = newExtent;
                    recreateCaptures();
                }

                FrameInfo frameInfo{ 
                    frameIndex, 
                    frameTime, 
                    commandBuffer, 
                    camera,
                    globalDescriptorSets[frameIndex], 
                    simObjects};
                
    
                GlobalUbo ubo{};
                ubo.projection = camera.getProjection();
                ubo.view = camera.getView();
                ubo.inverseView = glm::inverse(camera.getView());
                ubo.ambientLightColor = glm::vec4(1.f, 1.f, 1.f, 0.02f);
                
                ubo.sunDirection = glm::vec4(lightDir, 0.f);
                ubo.sunColor = sunColor;
                
                glm::vec3 lightPos = lightDir;
                glm::vec3 center   = glm::vec3(0.0f);

                glm::mat4 lightView = glm::lookAt(
                    lightPos,
                    center,
                    glm::vec3(0.0f, 1.0f, 0.0f));

                float orthoSize = 10.0f;
                glm::mat4 lightProj = glm::ortho(
                    -orthoSize, orthoSize,
                    -orthoSize, orthoSize,
                    1.0f, 50.0f);

                ubo.lightViewProj = lightProj * lightView;

                pointLightSystem.update(frameInfo, ubo);
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();

                VkClearValue clearDepth{};
                clearDepth.depthStencil = { 1.0f, 0 };

                VkRenderPassBeginInfo shadowRpInfo{};
                shadowRpInfo.sType               = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                shadowRpInfo.renderPass          = shadowRenderPass;
                shadowRpInfo.framebuffer         = shadowFramebuffer;
                shadowRpInfo.renderArea.offset   = {0, 0};
                shadowRpInfo.renderArea.extent   = { 2048u, 2048u };  
                shadowRpInfo.clearValueCount     = 1;
                shadowRpInfo.pClearValues        = &clearDepth;

                vkCmdBeginRenderPass(commandBuffer, &shadowRpInfo, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport shadowViewport{};
                shadowViewport.x        = 0.0f;
                shadowViewport.y        = 0.0f;
                shadowViewport.width    = 2048.0f;
                shadowViewport.height   = 2048.0f;
                shadowViewport.minDepth = 0.0f;
                shadowViewport.maxDepth = 1.0f;
                vkCmdSetViewport(commandBuffer, 0, 1, &shadowViewport);

                VkRect2D shadowScissor{};
                shadowScissor.offset = {0, 0};
                shadowScissor.extent = { 2048u, 2048u };
                vkCmdSetScissor(commandBuffer, 0, 1, &shadowScissor);

                shadowRenderSystem.renderSimObjects(frameInfo);

                vkCmdEndRenderPass(commandBuffer);

                renderer.beginSwapChainRenderPass(commandBuffer);

                skyboxRenderSystem.render(frameInfo);

                simpleRenderSystem.renderSimObjects(frameInfo);
                pointLightSystem.render(frameInfo);
                
                renderer.endSwapChainRenderPass(commandBuffer);
                renderer.copySwapImageToBuffer(commandBuffer, captures[frameIndex].buf);
                renderer.endFrame();
                
                ros.publishBGRA8(
                  extent.width, extent.height,
                  captures[frameIndex].mapped,
                  captures[frameIndex].size);
            }
        }
        for (auto& c : captures) {
            if (c.mapped) vkUnmapMemory(device.device(), c.mem);
            if (c.buf) vkDestroyBuffer(device.device(), c.buf, nullptr);
            if (c.mem) vkFreeMemory(device.device(), c.mem, nullptr);
            
        }
        vkDeviceWaitIdle(device.device());
        destroyShadowResources();
        destroySkyboxCubemap();
    }

    void SimApp::createSkyboxCubemap() {
        std::array<std::string, 6> faces = {
        
            "../assets/textures/skybox/right.jpg",
            "../assets/textures/skybox/left.jpg",
            "../assets/textures/skybox/bottom.jpg", // низ
            "../assets/textures/skybox/top.jpg", // верх
            "../assets/textures/skybox/front.jpg", //перед
             "../assets/textures/skybox/back.jpg"
        };

        int texWidth = 0, texHeight = 0, texChannels = 0;

        stbi_set_flip_vertically_on_load(false);

        std::vector<unsigned char> pixelData;
        pixelData.reserve(6 * 1024 * 1024);

        bool firstFace = true;
        VkDeviceSize faceSize = 0;

        for (size_t i = 0; i < faces.size(); ++i) {
            int w, h, ch;
            unsigned char* pixels = stbi_load(faces[i].c_str(), &w, &h, &ch, STBI_rgb_alpha);
            if (!pixels) {
                throw std::runtime_error("failed to load skybox face: " + faces[i]);
            }

            if (firstFace) {
                texWidth = w;
                texHeight = h;
                texChannels = 4; // принудительно RGBA8
                faceSize = static_cast<size_t>(texWidth) * texHeight * texChannels;
                pixelData.resize(faceSize * faces.size()); // ровно под 6 сторон
                firstFace = false;
            } else {
                if (w != texWidth || h != texHeight) {
                    stbi_image_free(pixels);
                    throw std::runtime_error("all skybox faces must have same resolution");
                }
            }

            if (i == 3 || i == 4) {
                int width = texWidth;
                int height = texHeight;
                int stride = texChannels;

                for (int y = 0; y < height; ++y) {
                    unsigned char* row = pixels + y * width * stride;
                    for (int x = 0; x < width / 2; ++x) {
                        unsigned char* pL = row + x * stride;
                        unsigned char* pR = row + (width - 1 - x) * stride;

                        // меняем местами RGBA (4 байта)
                        for (int c = 0; c < stride; ++c) {
                            std::swap(pL[c], pR[c]);
                        }
                    }
                }
            }

            // копируем данные этой грани в наш общий буфер
            std::memcpy(pixelData.data() + faceSize * i, pixels, faceSize);

            // освобождаем оригинальный буфер stb
            stbi_image_free(pixels);
        }

        // 2. Создаём staging-буфер и копируем туда все 6 граней

        VkDeviceSize imageSize = faceSize * faces.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        device.createBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory);

        void* data = nullptr;
        vkMapMemory(device.device(), stagingBufferMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixelData.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(device.device(), stagingBufferMemory);

        // 3. Создаём кубическую VkImage

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = static_cast<uint32_t>(texWidth);
        imageInfo.extent.height = static_cast<uint32_t>(texHeight);
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1;    // можно добавить mip'ы позже
        imageInfo.arrayLayers   = 6;    // 6 граней
        imageInfo.format        = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            skyboxImage,
            skyboxImageMemory);

        // 4. Переводим layout куба и копируем staging → image

        // Переход в TRANSFER_DST_OPTIMAL
        device.transitionImageLayout(
            skyboxImage,
            imageInfo.format,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            6);

        // Копируем сразу все 6 граней.
        // Важно: copyBufferToImage должна уметь работать с layerCount > 1.
        device.copyBufferToImage(
            stagingBuffer,
            skyboxImage,
            static_cast<uint32_t>(texWidth),
            static_cast<uint32_t>(texHeight),
            /*layerCount=*/6);

        // После копирования переводим в SHADER_READ_ONLY_OPTIMAL
        device.transitionImageLayout(
            skyboxImage,
            imageInfo.format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            6);

        // 5. Уничтожаем staging-буфер
        vkDestroyBuffer(device.device(), stagingBuffer, nullptr);
        vkFreeMemory(device.device(), stagingBufferMemory, nullptr);

        // 6. Создаём image view как CUBE

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = skyboxImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format   = imageInfo.format;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 6;

        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &skyboxImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create skybox image view");
        }

        // 7. Создаём sampler

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter    = VK_FILTER_LINEAR;
        samplerInfo.minFilter    = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.compareEnable    = VK_FALSE;
        samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &skyboxSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create skybox sampler");
    }
}
    void SimApp::createShadowRenderPass(VkFormat depthFormat) {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format         = depthFormat;
        depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 0;              
        subpass.pColorAttachments       = nullptr;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &depthAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if (vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow render pass");
        }
    }

    void SimApp::createShadowFramebuffer() {
        VkImageView attachments[] = { shadowImageView };

        shadowExtent = {SHADOW_MAP_WIDTH, SHADOW_MAP_HEIGHT};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = shadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = attachments;
        fbInfo.width           = shadowExtent.width;
        fbInfo.height          = shadowExtent.height;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(device.device(), &fbInfo, nullptr, &shadowFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow framebuffer");
        }
    }

    void SimApp::createShadowResources() {

        VkFormat depthFormat = device.findDepthFormat();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = SHADOW_MAP_WIDTH;
        imageInfo.extent.height = SHADOW_MAP_HEIGHT;
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.format        = depthFormat;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;           
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            shadowImage,
            shadowImageMemory);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = shadowImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = depthFormat;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &shadowImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow image view");
        }

        createShadowRenderPass(depthFormat);
        createShadowFramebuffer();

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter    = VK_FILTER_LINEAR;
        samplerInfo.minFilter    = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow sampler");
        }
    }

    void SimApp::loadSimObjects() {
        nlohmann::json scene;

        std::ifstream file("../assets/scene_config.json");
        if (!file.is_open()) {
            throw std::runtime_error("Can't open scene_config.json");
        }

        file >> scene;

        for (auto& obj : scene["objects"]) {
            std::string modelPath = obj["model"];

            std::shared_ptr<Model> model = Model::createModelFromFile(device, modelPath);

            auto simObj = SimObject::createSimObject();
            simObj.model = model;

            simObj.transform.translation = {
                obj["position"][0],
                obj["position"][1],
                obj["position"][2]
            };

            simObj.transform.rotation = { 
                obj["rotation"][0],
                obj["rotation"][1],
                obj["rotation"][2] 
            };

            simObj.transform.scale = { 
                obj["scale"][0],
                obj["scale"][1],
                obj["scale"][2] 
            };
            simObjects.emplace(simObj.getId(), std::move(simObj));
        }

        for (auto& light : scene["pointLights"]) {
            auto pointLight = SimObject::makePointLight(light["intensity"]);

            pointLight.color = {
                light["color"][0],
                light["color"][1],
                light["color"][2]
            };

            pointLight.transform.translation = {
                light["position"][0],
                light["position"][1],
                light["position"][2]
            };



            simObjects.emplace(pointLight.getId(), std::move(pointLight));
        }

        for (auto& sun : scene["sun"]) {
            auto sun_obj = SimObject::makePointLight(sun["intensity"], sun["radius"]);

            glm::vec3 color {
                sun["color"][0].get<float>(),
                sun["color"][1].get<float>(),
                sun["color"][2].get<float>(),
            };

            glm::vec3 pos {
                sun["position"][0].get<float>(),
                sun["position"][1].get<float>(), 
                sun["position"][2].get<float>()
            };

            sun_obj.transform.translation = pos;
            sun_obj.color = color;

            sunWorldPos = pos;
            lightDir = glm::normalize(glm::vec3(0.0f) - pos);
            
            sunColor = glm::vec4(color, sun["intensity"].get<float>()); 
            simObjects.emplace(sun_obj.getId(), std::move(sun_obj));
        } 
    }
}
