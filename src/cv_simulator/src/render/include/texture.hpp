#pragma once

#include "device.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>

namespace enginev {

class Texture {
public:
    Texture(Device& device, const std::string& imagePath);
    Texture(Device& device, uint8_t* pixels, int width, int height);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkDescriptorImageInfo getDescriptorInfo() const;
    VkSampler getSampler() const { return textureSampler; }
    VkImageView getImageView() const { return textureImageView; }

private:
    void createTextureImage(const std::string& imagePath);
    void createTextureImageView();
    void createTextureSampler();

    Device& device;
    VkImage textureImage{VK_NULL_HANDLE};
    VkDeviceMemory textureImageMemory{VK_NULL_HANDLE};
    VkImageView textureImageView{VK_NULL_HANDLE};
    VkSampler textureSampler{VK_NULL_HANDLE};
};

}