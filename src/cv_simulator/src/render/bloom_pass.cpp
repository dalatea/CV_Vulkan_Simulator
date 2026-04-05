#include "bloom_pass.hpp"

namespace enginev {

    BloomPass::BloomPass(Device& device)
        : device{ device } {}

    BloomPass::~BloomPass() {
        destroy();
    }

    void BloomPass::destroy() {
        if (framebufferA) {
            vkDestroyFramebuffer(device.device(), framebufferA, nullptr);
            framebufferA = VK_NULL_HANDLE;
        }
        if (framebufferB) {
            vkDestroyFramebuffer(device.device(), framebufferB, nullptr);
            framebufferB = VK_NULL_HANDLE;
        }

        if (renderPass) {
            vkDestroyRenderPass(device.device(), renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        if (samplerA) { vkDestroySampler(device.device(), samplerA, nullptr); samplerA = VK_NULL_HANDLE; }
        if (samplerB) { vkDestroySampler(device.device(), samplerB, nullptr); samplerB = VK_NULL_HANDLE; }

        if (viewA) { vkDestroyImageView(device.device(), viewA, nullptr); viewA = VK_NULL_HANDLE; }
        if (viewB) { vkDestroyImageView(device.device(), viewB, nullptr); viewB = VK_NULL_HANDLE; }

        if (imageA) { vkDestroyImage(device.device(), imageA, nullptr); imageA = VK_NULL_HANDLE; }
        if (imageB) { vkDestroyImage(device.device(), imageB, nullptr); imageB = VK_NULL_HANDLE; }

        if (memoryA) { vkFreeMemory(device.device(), memoryA, nullptr); memoryA = VK_NULL_HANDLE; }
        if (memoryB) { vkFreeMemory(device.device(), memoryB, nullptr); memoryB = VK_NULL_HANDLE; }

        extent = { 0,0 };
    }

    void BloomPass::recreate(VkExtent2D swapExtent, float newScale) {
        destroy();

        scale = newScale;
        extent = {
            std::max(1u, static_cast<uint32_t>(swapExtent.width * scale)),
            std::max(1u, static_cast<uint32_t>(swapExtent.height * scale))
        };

        createTargets();
        createRenderPass();
        createFramebuffers();
        createSamplers();
    }

    void BloomPass::createImage(VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = { extent.width, extent.height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = bloomFormat;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createImageWithInfo(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem);
    }

    void BloomPass::createView(VkImage img, VkImageView& view) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = bloomFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel = 0;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.device(), &vi, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("failed to create bloom image view");
        }
    }

    void BloomPass::createTargets() {
        createImage(imageA, memoryA);
        createImage(imageB, memoryB);

        createView(imageA, viewA);
        createView(imageB, viewB);
    }

    void BloomPass::createRenderPass() {
        VkAttachmentDescription color{};
        color.format = bloomFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // ����� DONT_CARE, �� CLEAR ��
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        if (vkCreateRenderPass(device.device(), &rp, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create bloom render pass");
        }
    }

    void BloomPass::createFramebuffers() {
        {
            VkImageView attachments[] = { viewA };
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = renderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = attachments;
            fb.width = extent.width;
            fb.height = extent.height;
            fb.layers = 1;

            if (vkCreateFramebuffer(device.device(), &fb, nullptr, &framebufferA) != VK_SUCCESS) {
                throw std::runtime_error("failed to create bloom framebuffer A");
            }
        }
        {
            VkImageView attachments[] = { viewB };
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = renderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = attachments;
            fb.width = extent.width;
            fb.height = extent.height;
            fb.layers = 1;

            if (vkCreateFramebuffer(device.device(), &fb, nullptr, &framebufferB) != VK_SUCCESS) {
                throw std::runtime_error("failed to create bloom framebuffer B");
            }
        }
    }

    void BloomPass::createSamplers() {
        VkSamplerCreateInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        s.magFilter = VK_FILTER_LINEAR;
        s.minFilter = VK_FILTER_LINEAR;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.anisotropyEnable = VK_FALSE;
        s.compareEnable = VK_FALSE;
        s.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(device.device(), &s, nullptr, &samplerA) != VK_SUCCESS ||
            vkCreateSampler(device.device(), &s, nullptr, &samplerB) != VK_SUCCESS) {
            throw std::runtime_error("failed to create bloom samplers");
        }
    }

    static void setFullscreenViewportScissor(VkCommandBuffer cmd, VkExtent2D extent) {
        VkViewport vp{};
        vp.x = 0.f; vp.y = 0.f;
        vp.width = static_cast<float>(extent.width);
        vp.height = static_cast<float>(extent.height);
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D sc{};
        sc.offset = { 0,0 };
        sc.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    void BloomPass::beginBright(VkCommandBuffer cmd) {
        VkClearValue clear{};
        clear.color = { {0.f,0.f,0.f,1.f} };

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebufferA;
        rp.renderArea.offset = { 0,0 };
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        setFullscreenViewportScissor(cmd, extent);
    }

    void BloomPass::endBright(VkCommandBuffer cmd) {
        vkCmdEndRenderPass(cmd);
    }

    void BloomPass::beginBlurH(VkCommandBuffer cmd) {
        VkClearValue clear{};
        clear.color = { {0.f,0.f,0.f,1.f} };

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebufferB; 
        rp.renderArea.offset = { 0,0 };
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        setFullscreenViewportScissor(cmd, extent);
    }

    void BloomPass::endBlurH(VkCommandBuffer cmd) {
        vkCmdEndRenderPass(cmd);
    }

    void BloomPass::beginBlurV(VkCommandBuffer cmd) {
        VkClearValue clear{};
        clear.color = { {0.f,0.f,0.f,1.f} };

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebufferA; 
        rp.renderArea.offset = { 0,0 };
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        setFullscreenViewportScissor(cmd, extent);
    }

    void BloomPass::endBlurV(VkCommandBuffer cmd) {
        vkCmdEndRenderPass(cmd);
    }

} // namespace enginev
