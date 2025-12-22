#pragma once

#include "device.hpp"

#include <vulkan/vulkan.h>
#include <array>
#include <stdexcept>

namespace enginev {

class ScenePass {
public:
    ScenePass(Device& device);
    ~ScenePass();

    ScenePass(const ScenePass&) = delete;
    ScenePass& operator=(const ScenePass&) = delete;

    // Создать/пересоздать ресурсы под новый extent
    void recreate(VkExtent2D extent);

    // Освобождение ресурсов (вызывается и из деструктора)
    void destroy();

    void begin(VkCommandBuffer cmd);
    void end(VkCommandBuffer cmd);

    // Доступ к ресурсам
    VkRenderPass  getRenderPass()   const { return sceneRenderPass; }
    VkFramebuffer getFramebuffer()  const { return sceneFramebuffer; }
    VkExtent2D    getExtent()       const { return sceneExtent; }

    VkImageView   getColorView()    const { return sceneColorView; }
    VkSampler     getColorSampler() const { return sceneColorSampler; }
    VkImage       getColorImage()   const { return sceneColorImage; }

    VkFormat      getColorFormat()  const { return sceneColorFormat; }
    VkFormat      getDepthFormat()  const { return sceneDepthFormat; }

    VkImageView getDepthView() const { return sceneDepthView; }
    VkSampler getDepthSampler() const { return sceneDepthSampler; }

private:
    void createSceneColorTarget(VkExtent2D extent);
    void createSceneDepthTarget(VkExtent2D extent);
    void createSceneRenderPass(VkFormat colorFormat, VkFormat depthFormat);
    void createSceneFramebuffer();

private:
    Device& device;

    VkExtent2D sceneExtent{0, 0};

    VkFormat sceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat sceneDepthFormat = VK_FORMAT_UNDEFINED;

    VkImage        sceneColorImage   = VK_NULL_HANDLE;
    VkDeviceMemory sceneColorMemory  = VK_NULL_HANDLE;
    VkImageView    sceneColorView    = VK_NULL_HANDLE;
    VkSampler      sceneColorSampler = VK_NULL_HANDLE;

    VkImage        sceneDepthImage   = VK_NULL_HANDLE;
    VkDeviceMemory sceneDepthMemory  = VK_NULL_HANDLE;
    VkImageView    sceneDepthView    = VK_NULL_HANDLE;
    VkSampler      sceneDepthSampler = VK_NULL_HANDLE;

    VkRenderPass   sceneRenderPass   = VK_NULL_HANDLE;
    VkFramebuffer  sceneFramebuffer  = VK_NULL_HANDLE;
};

}