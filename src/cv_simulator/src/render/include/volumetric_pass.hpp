#pragma once
#include "device.hpp"
#include "descriptors.hpp"
#include <unistd.h>
#include <filesystem>
#include <sstream>
#include<iostream>

namespace enginev {

class VolumetricPass {
public:
    VolumetricPass(Device& device, const std::string& compSpvPath = "volumetric_rays.comp.spv");
    ~VolumetricPass();
    
    void recreate(VkExtent2D swapExtent, float scale = 0.5f);
    void dispatch(VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
              const void* pushData = nullptr, uint32_t pushDataSize = 0);
    void transitionToGeneral(VkCommandBuffer cmd);
    void transitionToShaderRead(VkCommandBuffer cmd);
    
    VkImageView getView() const { return godRaysImageView_; }
    VkSampler getSampler() const { return godRaysSampler_; }
    VkDescriptorSetLayout getDescriptorSetLayout() const;
    
    void destroy();

private:
    Device& device_;
    std::string compSpvPath_;
    
    VkExtent2D godRaysExtent_{};
    VkFormat godRaysFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    
    VkImage godRaysImage_ = VK_NULL_HANDLE;
    VkDeviceMemory godRaysImageMemory_ = VK_NULL_HANDLE;
    VkImageView godRaysImageView_ = VK_NULL_HANDLE;
    VkSampler godRaysSampler_ = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::unique_ptr<DescriptorSetLayout> volSetLayout_;
    
    bool firstFrame_ = true;
    
    void createDescriptorSetLayout_();
    void createGodRaysImage_();
    void createGodRaysView_();
    void createGodRaysSampler_();
    void createPipelineLayout_();
    void createPipeline_();
    void destroyImage_();
    void destroyPipeline_();
    void destroyDescriptorSetLayout_(); 
    std::vector<char> readFile(const std::string& filepath);
    VkShaderModule createShaderModuleLocal(const std::vector<char>& code);
    std::string getShaderPath(const std::string& filename); 
};

} // namespace enginev