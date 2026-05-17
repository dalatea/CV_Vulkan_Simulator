#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <filesystem>
#include <sstream>
#include<iostream>

#include "camera_config.hpp"
namespace enginev {

class Device;
class DescriptorSetLayout;

class LensFlarePass {
public:
    LensFlarePass(Device& device, const std::string& compSpvPath = "lens_flare.comp.spv");
    ~LensFlarePass();

    LensFlarePass(const LensFlarePass&) = delete;
    LensFlarePass& operator=(const LensFlarePass&) = delete;

    void recreate(VkExtent2D swapExtent, float scale);
    void destroy();

    void transitionToGeneral(VkCommandBuffer cmd);
    void transitionToShaderRead(VkCommandBuffer cmd);

    void dispatch(VkCommandBuffer cmd, VkDescriptorSet lensSet);

    VkImage getFlareImage() const { return flareImage_; }
    VkImageView getFlareView() const { return flareImageView_; }
    VkSampler   getFlareSampler() const { return flareSampler_; }
    VkExtent2D  getExtent() const { return flareExtent_; }

    VkDescriptorSetLayout getDescriptorSetLayout() const;

private:
    void createDescriptorSetLayout_();
    void createFlareImage_();
    void createFlareView_();
    void createFlareSampler_();
    void createPipelineLayout_();
    void createPipeline_();

    void destroyImage_();
    void destroyPipeline_();
    void destroyDescriptorSetLayout_();

private:
    Device& device_;
    std::string compSpvPath_;

    VkExtent2D flareExtent_{};
    bool firstFrame_ = true;

    VkImage        flareImage_{VK_NULL_HANDLE};
    VkDeviceMemory flareImageMemory_{VK_NULL_HANDLE};
    VkImageView    flareImageView_{VK_NULL_HANDLE};
    VkSampler      flareSampler_{VK_NULL_HANDLE};
    VkFormat       flareFormat_{VK_FORMAT_R16G16B16A16_SFLOAT};

    // compute pipeline
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    VkPipeline       pipeline_{VK_NULL_HANDLE};
    std::vector<char> readFile(const std::string& filepath);
    VkShaderModule createShaderModuleLocal(const std::vector<char>& code);
    std::string getShaderPath(const std::string& filename); 

    // descriptor layout (custom wrapper)
    std::unique_ptr<DescriptorSetLayout> lensSetLayout_;
};

}