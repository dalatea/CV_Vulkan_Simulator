#pragma once

#include "device.hpp"

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <array>

namespace enginev {

class BloomPass {
public:
    explicit BloomPass(Device& device);
    ~BloomPass();

    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    void recreate(VkExtent2D swapExtent, float scale = 0.5f);
    void destroy();

    // bright pass
    void beginBright(VkCommandBuffer cmd);
    void endBright(VkCommandBuffer cmd);

    // blur passes
    void beginBlurH(VkCommandBuffer cmd);
    void endBlurH(VkCommandBuffer cmd);

    void beginBlurV(VkCommandBuffer cmd);
    void endBlurV(VkCommandBuffer cmd);

    // getters
    VkExtent2D getExtent() const { return extent; }

    VkRenderPass getRenderPass() const { return renderPass; }

    VkFramebuffer getFramebufferA() const { return framebufferA; }
    VkFramebuffer getFramebufferB() const { return framebufferB; }

    VkImageView getViewA() const { return viewA; }
    VkImageView getViewB() const { return viewB; }

    VkSampler getSamplerA() const { return samplerA; }
    VkSampler getSamplerB() const { return samplerB; }

private:
    void createTargets();
    void createRenderPass();
    void createFramebuffers();
    void createSamplers();

    void createImage(VkImage& img, VkDeviceMemory& mem);
    void createView(VkImage img, VkImageView& view);

private:
    Device& device;

    VkExtent2D extent{};
    float scale = 0.5f;

    const VkFormat bloomFormat = VK_FORMAT_R8G8B8A8_UNORM;

    VkImage imageA{VK_NULL_HANDLE};
    VkDeviceMemory memoryA{VK_NULL_HANDLE};
    VkImageView viewA{VK_NULL_HANDLE};
    VkSampler samplerA{VK_NULL_HANDLE};

    VkImage imageB{VK_NULL_HANDLE};
    VkDeviceMemory memoryB{VK_NULL_HANDLE};
    VkImageView viewB{VK_NULL_HANDLE};
    VkSampler samplerB{VK_NULL_HANDLE};

    VkRenderPass renderPass{VK_NULL_HANDLE};

    VkFramebuffer framebufferA{VK_NULL_HANDLE};
    VkFramebuffer framebufferB{VK_NULL_HANDLE};
};

} // namespace enginev