#include "app.hpp"

#include "keyboard_movement_controller.hpp"
#include "simple_render_system.hpp"
#include "point_light_system.hpp"
#include "shadow_render_system.hpp"
#include "skybox_render_system.hpp"
#include "buffer.hpp"
#include "ros_bridge.hpp"
#include "post_process_render_system.hpp"
#include "bright_render_system.hpp"
#include "blur_render_system.hpp"
#include "exposure_reduce_system.hpp"
#include "exposure_update_system.hpp"

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
#include <cstdlib>  
#include <iostream>  
#include <cmath>


namespace cvsim {


    SimApp::SimApp()
        : SimApp(StressConfig{}) {}   

    SimApp::SimApp(const StressConfig& stressCfg)
        : stressCfg_{ stressCfg } {

        globalPool =
            DescriptorPool::Builder(device)
            .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 10)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 6)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * 12)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SwapChain::MAX_FRAMES_IN_FLIGHT * 2)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 10)
            .build();

        scenePass = std::make_unique<enginev::ScenePass>(device);
        bloomPass = std::make_unique<enginev::BloomPass>(device);
        lensFlarePass = std::make_unique<LensFlarePass>(device);

        createShadowResources();
        createSkyboxCubemap();

        loadSimObjects();
    }

    SimApp::~SimApp() {}

    std::shared_ptr<Model> SimApp::getModelCached_(const std::string& modelPath) {
        auto it = modelCache_.find(modelPath);
        if (it != modelCache_.end()) {
            return it->second;
        }

        std::shared_ptr<Model> model = {Model::createModelFromFile(device, modelPath)};
        modelCache_.emplace(modelPath, model);
        return model;
    }

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
        
        std::vector<LensSurfaceGPU> lensSurfacesCpu = {
            // radius,   z,     ior,  aperture, isStop
            {  0.050f,  0.000f, 1.5f, 0.020f, 0, 0,0,0 }, // front element
            { -0.050f,  0.010f, 1.0f, 0.020f, 0, 0,0,0 }, // exit of element (air)
            {  0.030f,  0.020f, 1.6f, 0.018f, 0, 0,0,0 },
            { -0.030f,  0.028f, 1.0f, 0.018f, 0, 0,0,0 },
            {  0.0f,    0.035f, 1.0f, 0.012f, 1, 0,0,0 }, // aperture stop
            {  0.040f,  0.040f, 1.5f, 0.020f, 0, 0,0,0 },
            { -0.040f,  0.050f, 1.0f, 0.020f, 0, 0,0,0 },
        };

        // storage buffer
        auto lensSurfacesBuffer = std::make_unique<Buffer>(
            device,
            sizeof(enginev::LensSurfaceGPU),
            static_cast<uint32_t>(lensSurfacesCpu.size()),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        lensSurfacesBuffer->map();
        lensSurfacesBuffer->writeToBuffer(lensSurfacesCpu.data());
        lensSurfacesBuffer->flush();

        // LensParams
        std::vector<std::unique_ptr<Buffer>> lensParamsBuffers(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < static_cast<int>(lensParamsBuffers.size()); ++i) {
            lensParamsBuffers[i] = std::make_unique<Buffer>(
                device,
                sizeof(enginev::LensParamsGPU),
                1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            );
            lensParamsBuffers[i]->map();
        }

        auto exposureData = std::make_unique<Buffer>(
                    device,
                    sizeof(float) + sizeof(int),
                    1,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
                exposureData->map();

                // initial data
                struct {
                    float logLumSum = 0.0f;
                    int pixelCount = 0;
                } expDataInit;
                exposureData->writeToBuffer(&expDataInit);

                auto exposureState = std::make_unique<Buffer>(
                    device,
                    sizeof(float) * 5,
                    1,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );
                exposureState->map();

                struct {
                    float autoExposure = 1.0f;
                    float targetExposure = 1.0f;
                    float adaptionRateUp = 1.5f;
                    float adaptionRateDown = 3.5f;
                    float dt = 0.016f;
                } expStateInit;

                exposureState->writeToBuffer(&expStateInit);
    
        auto globalSetLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        auto brightSetLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        auto blurSetLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        auto postSetLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        auto lensSetLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_SHADER_STAGE_COMPUTE_BIT)  // flare out
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // lens surfaces
            .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // lens params
            .addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // GlobalUbo
            .build();

        auto exposureReduceLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
        
        auto exposureUpdateLayout =
            DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        std::vector<VkDescriptorSet> globalDescriptorSets(SwapChain::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSet> brightDescriptorSets(SwapChain::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSet> blurDescriptorSetsH(SwapChain::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSet> blurDescriptorSetsV(SwapChain::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSet> lensDescriptorSets(SwapChain::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSet> exposureReduceDescriptorSet(SwapChain::MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSet> exposureUpdateDescriptorSet(SwapChain::MAX_FRAMES_IN_FLIGHT);

        VkDescriptorImageInfo hdrImageInfo{};
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrImageInfo.imageView = scenePass->getColorView();
        hdrImageInfo.sampler = scenePass->getColorSampler();

        auto expDataInfo = exposureData->descriptorInfo();
        auto expStateInfo = exposureState->descriptorInfo();

        VkDescriptorSet exposureReduceSet;
        DescriptorWriter(*exposureReduceLayout, *globalPool)
        .writeImage(0, &hdrImageInfo)
        .writeBuffer(1, &expDataInfo)
        .build(exposureReduceSet);

        VkDescriptorSet exposureUpdateSet;
        DescriptorWriter(*exposureUpdateLayout, *globalPool)
        .writeBuffer(0, &expDataInfo)
        .writeBuffer(1, &expStateInfo)
        .build(exposureUpdateSet);

        postDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

        auto extent = renderer.getSwapChainExtent();
        scenePass->recreate(extent);
        bloomPass->recreate(extent, 0.5f);
        lensFlarePass->recreate(renderer.getSwapChainExtent(), 1.0f);

        VkDescriptorImageInfo sceneColorInfo{};
        sceneColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneColorInfo.imageView   = scenePass->getColorView();     
        sceneColorInfo.sampler     = scenePass->getColorSampler();

        VkDescriptorImageInfo bloomAInfo{};
        bloomAInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bloomAInfo.imageView   = bloomPass->getViewA();
        bloomAInfo.sampler     = bloomPass->getSamplerA();

        VkDescriptorImageInfo bloomBInfo{};
        bloomBInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bloomBInfo.imageView   = bloomPass->getViewB();
        bloomBInfo.sampler     = bloomPass->getSamplerB();

        VkDescriptorImageInfo sceneDepthInfo{};
        sceneDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sceneDepthInfo.imageView = scenePass->getDepthView();
        sceneDepthInfo.sampler = scenePass->getDepthSampler();

        VkDescriptorImageInfo flareSampledInfo{};
        flareSampledInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        flareSampledInfo.imageView = lensFlarePass->getFlareView();
        flareSampledInfo.sampler = lensFlarePass->getFlareSampler();

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
            DescriptorWriter(*brightSetLayout, *globalPool)
                .writeImage(0, &sceneColorInfo)
                .build(brightDescriptorSets[i]);
        }

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
            DescriptorWriter(*blurSetLayout, *globalPool)
                .writeImage(0, &bloomAInfo)
                .build(blurDescriptorSetsH[i]);

            DescriptorWriter(*blurSetLayout, *globalPool)
                .writeImage(0, &bloomBInfo)
                .build(blurDescriptorSetsV[i]);
        }

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();

            enginev::DescriptorWriter(*postSetLayout, *globalPool)
                .writeImage(0, &sceneColorInfo)
                .writeImage(1, &bloomAInfo)
                .writeBuffer(2, &bufferInfo)
                .writeImage(3, &sceneDepthInfo)
                .writeImage(4, &flareSampledInfo)
                .build(postDescriptorSets[i]);
        }

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            DescriptorWriter(*exposureReduceLayout, *globalPool)
                .writeImage(0, &hdrImageInfo)
                .writeBuffer(1, &expDataInfo)
                .build(exposureReduceDescriptorSet[i]);
        }

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            DescriptorWriter(*exposureUpdateLayout, *globalPool)
                .writeBuffer(0, &expDataInfo)
                .writeBuffer(1, &expStateInfo)
                .build(exposureUpdateDescriptorSet[i]);
        }

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
                .writeBuffer(0, &bufferInfo)     
                .writeImage(1, &shadowImageInfo) 
                .writeImage(2, &skyboxImageInfo)
                .build(globalDescriptorSets[i]);
        }

        for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {

            VkDescriptorImageInfo flareStorageInfo{};
            flareStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            flareStorageInfo.imageView   = lensFlarePass->getFlareView();
            flareStorageInfo.sampler     = VK_NULL_HANDLE; // storage image

            auto lensSurfInfo   = lensSurfacesBuffer->descriptorInfo();
            auto lensParamsInfo = lensParamsBuffers[i]->descriptorInfo();
            auto globalInfo     = uboBuffers[i]->descriptorInfo();

            DescriptorWriter(*lensSetLayout, *globalPool)
                .writeImage(0, &flareStorageInfo)
                .writeBuffer(1, &lensSurfInfo)
                .writeBuffer(2, &lensParamsInfo)
                .writeBuffer(3, &globalInfo)
                .build(lensDescriptorSets[i]);
        }

        SimpleRenderSystem simpleRenderSystem{
            device,
            scenePass->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout() };
            
        ShadowRenderSystem shadowRenderSystem{
            device,
            shadowRenderPass,
            globalSetLayout->getDescriptorSetLayout() };
        PointLightSystem pointLightSystem{
           device,
           scenePass->getRenderPass(),
           globalSetLayout->getDescriptorSetLayout() };

        SkyboxRenderSystem skyboxRenderSystem(
            device,
            scenePass->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout()
        );

        BrightExtractRenderSystem brightExtractSystem(
            device,
            bloomPass->getRenderPass(),
            brightSetLayout->getDescriptorSetLayout()
        );

        BlurRenderSystem blurHSystem(
            device,
            bloomPass->getRenderPass(),
            blurSetLayout->getDescriptorSetLayout(),
            true
        );

        BlurRenderSystem blurVSystem(
            device,
            bloomPass->getRenderPass(),
            blurSetLayout->getDescriptorSetLayout(),
            false
        );

        PostProcessRenderSystem postProcessSystem(
            device,
            renderer.getSwapChainRenderPass(),
            postSetLayout->getDescriptorSetLayout()
        );

        ExposureReduceSystem exposureReduceSystem(device);
        ExposureUpdateSystem exposureUpdateSystem(device);

        std::shared_ptr<Model> skyboxModel = Model::createSkyboxCube(device);
        
        KeyboardMovementController cameraController{};

        std::vector<CameraRig> cameras;
        cameras.reserve(3);

        cameras.push_back(CameraRig::MakeCam(glm::vec3(0.f, 0.f, -2.5f), CameraControlType::Keyboard));

        cameras.push_back(CameraRig::MakeCam(glm::vec3(2.f, 1.f, -2.5f), CameraControlType::ROS));
        cameras.push_back(CameraRig::MakeCam(glm::vec3(2.f, 1.f, -2.5f), CameraControlType::ROS));

        int activeCam = 0;

        auto currentTime = std::chrono::high_resolution_clock::now();

        struct FrameCapture { VkBuffer buf{}; VkDeviceMemory mem{}; void* mapped{}; size_t size{}; };
        std::array<FrameCapture, enginev::SwapChain::MAX_FRAMES_IN_FLIGHT> captures;
        
        RosImageBridge ros;

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

        double fpsWindowTime = 0.0;
        std::uint64_t fpsWindowFrames = 0;

        double totalTime = 0.0;
        std::uint64_t totalFrames = 0;

        const double fpsPrintPeriod = 1.0;

        while (!window.shouldClose()) {
            glfwPollEvents();

            static bool cWasPressed = false;
            bool cPressed = glfwGetKey(window.getGLFWwindow(), GLFW_KEY_C) == GLFW_PRESS;

            if (cPressed && !cWasPressed) {
                activeCam = (activeCam + 1) % static_cast<int>(cameras.size());
            }
            cWasPressed = cPressed;

            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime =
                std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;

            auto cmd = ros.getLastCmd();
            
            for (size_t i = 0; i < cameras.size(); ++i)
            {
                auto& cam = cameras[i];

                if (static_cast<int>(i) == activeCam)
                {
                    if (cam.control == CameraControlType::Keyboard) {
                        cameraController.moveInPlaneXZ(
                            window.getGLFWwindow(), frameTime, cam.rig
                        );
                    } else {

                            cameraController.moveInPlaneXZ(
                                window.getGLFWwindow(), frameTime, cam.rig
                            );
                            cam.applyRos(frameTime, cmd);
                        
                    }
                }

                cam.camera.setViewYXZ(cam.rig.transform.translation, cam.rig.transform.rotation);
            }

            float aspect = renderer.getAspectRatio();
            for (auto& cam : cameras) {
                cam.camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.f);
            }

            enginev::Camera& camera = cameras[activeCam].camera;
            glm::mat4 VP = camera.getProjection() * camera.getView();
            Frustum frustum = extractFrustum(VP);

            if (auto commandBuffer = renderer.beginFrame()) {
                int frameIndex = renderer.getFrameIndex();

                VkExtent2D newExtent = renderer.getSwapChainExtent();

                if (newExtent.width != extent.width ||
                newExtent.height != extent.height) {
                    vkDeviceWaitIdle(device.device());
                    extent = newExtent;
                    recreateCaptures();

                    scenePass->recreate(extent);
                    bloomPass->recreate(extent, 0.5f);
                    lensFlarePass->recreate(renderer.getSwapChainExtent(), 1.0f);

                    VkDescriptorImageInfo sceneColorInfo{};
                    sceneColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    sceneColorInfo.imageView   = scenePass->getColorView();
                    sceneColorInfo.sampler     = scenePass->getColorSampler();

                    VkDescriptorImageInfo bloomAInfo{};
                    bloomAInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    bloomAInfo.imageView   = bloomPass->getViewA();
                    bloomAInfo.sampler     = bloomPass->getSamplerA();

                    VkDescriptorImageInfo bloomBInfo{};
                    bloomBInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    bloomBInfo.imageView   = bloomPass->getViewB();
                    bloomBInfo.sampler     = bloomPass->getSamplerB();

                    VkDescriptorImageInfo sceneDepthInfo{};
                    sceneDepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    sceneDepthInfo.imageView = scenePass->getDepthView();
                    sceneDepthInfo.sampler = scenePass->getDepthSampler();

                    VkDescriptorImageInfo flareSampledInfo{};
                    flareSampledInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    flareSampledInfo.imageView   = lensFlarePass->getFlareView();
                    flareSampledInfo.sampler     = lensFlarePass->getFlareSampler();

                    for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
                        DescriptorWriter(*brightSetLayout, *globalPool)
                            .writeImage(0, &sceneColorInfo)
                            .build(brightDescriptorSets[i]);
                    }

                     for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
                        DescriptorWriter(*blurSetLayout, *globalPool)
                            .writeImage(0, &bloomAInfo)
                            .build(blurDescriptorSetsH[i]);

                        DescriptorWriter(*blurSetLayout, *globalPool)
                            .writeImage(0, &bloomBInfo)
                            .build(blurDescriptorSetsV[i]);
                            }

                    for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
                        auto bufferInfo = uboBuffers[i]->descriptorInfo();

                        DescriptorWriter(*postSetLayout, *globalPool)
                            .writeImage(0, &sceneColorInfo)
                            .writeImage(1, &bloomAInfo)
                            .writeBuffer(2, &bufferInfo)
                            .writeImage(3, &sceneDepthInfo)
                            .writeImage(4, &flareSampledInfo)
                            .build(postDescriptorSets[i]);
                    }

                    for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
                        VkDescriptorImageInfo flareStorageInfo{};
                        flareStorageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        flareStorageInfo.imageView   = lensFlarePass->getFlareView();
                        flareStorageInfo.sampler     = VK_NULL_HANDLE;

                        auto lensSurfInfo   = lensSurfacesBuffer->descriptorInfo();
                        auto lensParamsInfo = lensParamsBuffers[i]->descriptorInfo();
                        auto globalInfo     = uboBuffers[i]->descriptorInfo();

                        DescriptorWriter(*lensSetLayout, *globalPool)
                            .writeImage(0, &flareStorageInfo)
                            .writeBuffer(1, &lensSurfInfo)
                            .writeBuffer(2, &lensParamsInfo)
                            .writeBuffer(3, &globalInfo)
                            .build(lensDescriptorSets[i]);
                    }
                }
                FrameInfo frameInfo{ 
                    frameIndex, 
                    frameTime, 
                    commandBuffer, 
                    camera,
                    globalDescriptorSets[frameIndex], 
                    simObjects};
                
                frameInfo.frustum = frustum;
                GlobalUbo ubo{};
                ubo.projection = camera.getProjection();
                ubo.view = camera.getView();
                ubo.inverseView = glm::inverse(camera.getView());

                glm::mat4 invView = ubo.inverseView;

                glm::mat4 V = camera.getView();
                glm::mat4 P = camera.getProjection();
                glm::mat4 invV = glm::inverse(V);

                glm::vec3 camPos = glm::vec3(invV[3]);
                glm::vec3 camForward = glm::normalize(-glm::vec3(invV[2]));

                glm::vec3 sunWorld = camPos + (-lightDir) * 10000.0f;
                glm::vec3 sunWorldInv = camPos + (lightDir) * 10000.0f;
                glm::vec3 sunViewDir = glm::normalize(sunWorldInv - camPos);
                float dotFS = glm::clamp(glm::dot(camForward, sunViewDir), 0.0f, 1.0f);
                float sunFactor = glm::smoothstep(0.70f, 0.95f, dotFS);

                ubo.sunParams = glm::vec4(sunFactor, 0.f, 0.f, 0.f);


                glm::vec4 clip = P * V * glm::vec4(sunWorld, 1.0f);

                glm::vec2 sunUV(0.5f);
                float visibility = 0.0f;

                if (clip.w > 0.0f) {
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;

                    sunUV = glm::vec2(ndc.x, ndc.y) * 0.5f + glm::vec2(0.5f);

                    const float sunCosSize = 0.995f;
                    float sunTheta = acos(sunCosSize);
                    float tanTheta = tan(sunTheta);

                    float P00 = P[0][0];
                    float P11 = P[1][1];

                    float rNdcX = tanTheta * P00;
                    float rNdcY = tanTheta * P11;

                    float rUvX = rNdcX * 0.5f;
                    float rUvY = rNdcY * 0.5f;

                    bool intersects = 
                        sunUV.x >= -rUvX && sunUV.x <= 1.0f + rUvX &&
                        sunUV.y >= -rUvY && sunUV.y <= 1.0f + rUvY;
                    
                    visibility = intersects ? 1.0f : 0.0f;
                }

                ubo.sunScreen = glm::vec4(sunUV, visibility, 1.0f);

                ubo.ambientLightColor = glm::vec4(1.0f, 0.95f, 0.7f, 0.15f);
                
                ubo.sunDirection = glm::vec4(lightDir, 0.f);
                ubo.sunColor = sunColor;
                
                glm::vec3 L = glm::normalize(lightDir); 
                glm::vec3 center   = glm::vec3(0.0f);
                glm::vec3 lightPos = center - L * 50.0f;

                glm::mat4 lightView = glm::lookAtRH(
                    lightPos,
                    center,
                    glm::vec3(0.0f, 1.0f, 0.0f));

                float orthoSize = 10.0f;
                glm::mat4 lightProj = glm::orthoRH_ZO(
                    -orthoSize, orthoSize,
                    -orthoSize, orthoSize,
                    0.1f, 80.0f);

                ubo.lightViewProj = lightProj * lightView;

                pointLightSystem.update(frameInfo, ubo);
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();

                LensParamsGPU lensParams{};
                lensParams.surfaceCount = static_cast<int>(lensSurfacesCpu.size());
                lensParams.sensorZ = 0.060f;

                lensParams.sensorW = 0.036f;
                lensParams.sensorH = 0.024f;

                lensParams.sensorW = 0.036f;
                lensParams.sensorH = 0.024f;

                lensParamsBuffers[frameIndex]->writeToBuffer(&lensParams);
                lensParamsBuffers[frameIndex]->flush();

                VkClearValue clearDepth{};
                clearDepth.depthStencil = { 1.0f, 0 };

                VkRenderPassBeginInfo shadowRpInfo{};
                shadowRpInfo.sType               = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                shadowRpInfo.renderPass          = shadowRenderPass;
                shadowRpInfo.framebuffer         = shadowFramebuffer;
                shadowRpInfo.renderArea.offset   = {0, 0};
                shadowRpInfo.renderArea.extent   = { 4096u, 4096u };  
                shadowRpInfo.clearValueCount     = 1;
                shadowRpInfo.pClearValues        = &clearDepth;

                vkCmdBeginRenderPass(commandBuffer, &shadowRpInfo, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport shadowViewport{};
                shadowViewport.x        = 0.0f;
                shadowViewport.y        = 0.0f;
                shadowViewport.width    = static_cast<float>(shadowExtent.width);
                shadowViewport.height   = static_cast<float>(shadowExtent.height);
                shadowViewport.minDepth = 0.0f;
                shadowViewport.maxDepth = 1.0f;
                vkCmdSetViewport(commandBuffer, 0, 1, &shadowViewport);

                VkRect2D shadowScissor{};
                shadowScissor.offset = {0, 0};
                shadowScissor.extent = shadowExtent;
                vkCmdSetScissor(commandBuffer, 0, 1, &shadowScissor);

                shadowRenderSystem.renderSimObjects(frameInfo);

                vkCmdEndRenderPass(commandBuffer);

                scenePass->begin(commandBuffer);

                skyboxRenderSystem.render(frameInfo);
                simpleRenderSystem.renderSimObjects(frameInfo);
                pointLightSystem.render(frameInfo);

                scenePass->end(commandBuffer);
                
                BrightPushConstant brightPC{};
                brightPC.threshold = 0.85f;
                brightPC.knee = 0.08f;

                bloomPass->beginBright(commandBuffer);
                brightExtractSystem.render(frameInfo, brightDescriptorSets[frameIndex], brightPC);
                bloomPass->endBright(commandBuffer);
                
                BlurPushConstant blurPC{};
                blurPC.texelSize = {
                    1.0f / extent.width,
                     1.0f / extent.height
                };
                blurPC.radius = 5.0f;

                bloomPass->beginBlurH(commandBuffer);
                blurHSystem.render(frameInfo, blurDescriptorSetsH[frameIndex], blurPC);
                bloomPass->endBlurH(commandBuffer);

                bloomPass->beginBlurV(commandBuffer);
                blurVSystem.render(frameInfo, blurDescriptorSetsV[frameIndex], blurPC);
                bloomPass->endBlurV(commandBuffer);

                lensFlarePass->transitionToGeneral(commandBuffer);
                lensFlarePass->dispatch(commandBuffer, lensDescriptorSets[frameIndex]);
                lensFlarePass->transitionToShaderRead(commandBuffer);

                exposureReduceSystem.dispatch(
                    commandBuffer,
                    extent,
                    exposureReduceDescriptorSet[frameIndex]
                );

                VkMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    1, &barrier,
                    0, nullptr,
                    0, nullptr
                );

                exposureUpdateSystem.dispatch(
                    commandBuffer,
                    exposureUpdateDescriptorSet[frameIndex]
                );

                ExposureState cpuExp{};
                std::memcpy(&cpuExp, exposureState->getMappedMemory(), sizeof(ExposureState));

                ubo.autoExposure = cpuExp.autoExposure;     
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();

                renderer.beginSwapChainRenderPass(commandBuffer);
                postProcessSystem.render(frameInfo, postDescriptorSets[frameIndex]);
                renderer.endSwapChainRenderPass(commandBuffer);


                renderer.copySwapImageToBuffer(commandBuffer, captures[frameIndex].buf);
                renderer.endFrame();

                fpsWindowTime += frameTime;
                fpsWindowFrames += 1;

                totalTime += frameTime;
                totalFrames += 1;

                if (fpsWindowTime >= fpsPrintPeriod) {
                    const double fps = static_cast<double>(fpsWindowFrames) / fpsWindowTime;
                    std::cout << "FPS: " << fps << std::endl;

                    fpsWindowTime = 0.0;
                    fpsWindowFrames = 0;
                }
                
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
        if (lensFlarePass) 
        {
            lensFlarePass->destroy();
            lensFlarePass.reset();
        }

        bloomPass->destroy();
        scenePass->destroy();

        destroyShadowResources();
        destroySkyboxCubemap();

        if (totalTime > 0.0) {
            const double avgFps = static_cast<double>(totalFrames) / totalTime;
            std::cout << "\n\n\n\n\n\n\n\nAverage FPS: " << avgFps
                      << " (frames=" << totalFrames
                      << ", time=" << totalTime << "s)\n\n\n\n\n\n\n\n" << std::endl;
        } else {
            std::cout << "Average FPS: no, totalTime=0" << std::endl;
        }
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
                texChannels = 4; 
                faceSize = static_cast<size_t>(texWidth) * texHeight * texChannels;
                pixelData.resize(faceSize * faces.size()); 
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

                        for (int c = 0; c < stride; ++c) {
                            std::swap(pL[c], pR[c]);
                        }
                    }
                }
            }

            std::memcpy(pixelData.data() + faceSize * i, pixels, faceSize);

            stbi_image_free(pixels);
        }

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

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = static_cast<uint32_t>(texWidth);
        imageInfo.extent.height = static_cast<uint32_t>(texHeight);
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1;    
        imageInfo.arrayLayers   = 6;    
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

        device.transitionImageLayout(
            skyboxImage,
            imageInfo.format,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            6);

        device.copyBufferToImage(
            stagingBuffer,
            skyboxImage,
            static_cast<uint32_t>(texWidth),
            static_cast<uint32_t>(texHeight),
            6);

        device.transitionImageLayout(
            skyboxImage,
            imageInfo.format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            6);

        vkDestroyBuffer(device.device(), stagingBuffer, nullptr);
        vkFreeMemory(device.device(), stagingBufferMemory, nullptr);

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

        std::array<VkSubpassDependency, 2> deps{};

        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        renderPassInfo.pDependencies = deps.data();

        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &depthAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;

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
        samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.mipLodBias = 0.0f;

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

        const std::string path = (!stressCfg_.scenePath.empty()) 
            ? stressCfg_.scenePath
            : "../assets/scene_config.json";

        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Can't open scene_config.json");
        }

        file >> scene;

        if (scene.contains("pointLights")) {
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
        }
        if (scene.contains("sun")) {
            for (auto& sun : scene["sun"]) {
                glm::vec3 color {
                    sun["color"][0].get<float>(),
                    sun["color"][1].get<float>(),
                    sun["color"][2].get<float>(),
                };

                glm::vec3 pos {
                    sun["direction"][0].get<float>(),
                    sun["direction"][1].get<float>(), 
                    sun["direction"][2].get<float>()
                };

                lightDir = glm::normalize(pos);
                sunColor = glm::vec4(color, sun["intensity"].get<float>());
            } 
        }
        if (stressCfg_.enabled) {
            const int stressCount = (stressCfg_.count > 0) ? stressCfg_.count : 50000;
            const float spacing = (stressCfg_.spacing > 0.0f) ? stressCfg_.spacing : 2.0f;

            std::string modelPath = stressCfg_.modelPath;
            if (modelPath.empty()) {
                if (scene.contains("objects") && !scene["objects"].empty() && scene["objects"][0].contains("model")) {
                    modelPath = scene["objects"][0]["model"].get<std::string>();
                }
            }
            if (modelPath.empty()) {
                throw std::runtime_error(
                    "Stress mode: model path is empty. Provide --stress-model PATH or put at least one object in scene_config.json"
                );
            }

            std::shared_ptr<Model> sharedModel = getModelCached_(modelPath);

            simObjects.reserve(simObjects.size() + static_cast<size_t>(stressCount) + 16);

            const int side = static_cast<int>(std::ceil(std::cbrt(static_cast<double>(stressCount))));
            const float half = 0.5f * static_cast<float>(side - 1);

            const float stressLightIntensity = 0.1f;
            const glm::vec3 stressLightColor = {0.1f, 0.1f, 1.0f};
            const float halfStep = 0.5f * spacing;

            int created = 0;
            for (int x = 0; x < side && created < stressCount; ++x) {
                for (int y = 0; y < side && created < stressCount; ++y) {
                    for (int z = 0; z < side && created < stressCount; ++z) {
                        auto simObj = SimObject::createSimObject();
                        simObj.model = sharedModel;

                        glm::vec3 objPos{
                            (static_cast<float>(x) - half) * spacing,
                            (static_cast<float>(y) - half) * spacing,
                            (static_cast<float>(z) - half) * spacing
                        };
                        simObj.transform.translation = objPos;
                        simObj.transform.rotation = { 0.0f, 0.0f, 0.0f };
                        simObj.transform.scale = { 1.0f, 1.0f, 1.0f };

                        simObjects.emplace(simObj.getId(), std::move(simObj));
                        ++created;

                        auto light = SimObject::makePointLight(stressLightIntensity);
                        light.color = stressLightColor;

                        float sx = (x < side - 1) ? +halfStep : -halfStep;
                        float sy = (y < side - 1) ? +halfStep : -halfStep;
                        float sz = (z < side - 1) ? +halfStep : -halfStep;

                        light.transform.translation = objPos + glm::vec3(sx, sy, sz);

                        simObjects.emplace(light.getId(), std::move(light));

                        ++created;

                    }
                }
            }

            std::cout << "[STRESS] Enabled: spawned " << created
                << " objects, model=" << modelPath
                << ", spacing=" << spacing << "\n";

            return; 
        }

        if (scene.contains("objects")) {
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
        }
    }
}