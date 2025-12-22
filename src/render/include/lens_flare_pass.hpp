#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

namespace enginev {

class Device;
class DescriptorSetLayout;

struct LensSurfaceGPU {
    float radius;     // signed
    float z;
    float ior;
    float aperture;
    int   isStop;
    int   pad0;
    int   pad1;
    int   pad2;
};

struct LensParamsGPU {
    int   surfaceCount;
    float sensorZ;
    float sensorW;
    float sensorH;
};

class LensFlarePass {
public:
    LensFlarePass(Device& device, const std::string& compSpvPath = "shaders/lens_flare.comp.spv");
    ~LensFlarePass();

    LensFlarePass(const LensFlarePass&) = delete;
    LensFlarePass& operator=(const LensFlarePass&) = delete;

    // scale обычно 0.5f
    void recreate(VkExtent2D swapExtent, float scale);
    void destroy();

    // Layout helpers для compute-write -> sample-read
    void transitionToGeneral(VkCommandBuffer cmd);
    void transitionToShaderRead(VkCommandBuffer cmd);

    // Запуск compute
    void dispatch(VkCommandBuffer cmd, VkDescriptorSet lensSet);

    // Доступ для post.frag
    VkImageView getFlareView() const { return flareImageView_; }
    VkSampler   getFlareSampler() const { return flareSampler_; }
    VkExtent2D  getExtent() const { return flareExtent_; }

    // Layout для выделения и записи дескрипторов снаружи
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

    // flare image (storage+sampled)
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

    // descriptor layout (custom wrapper)
    std::unique_ptr<DescriptorSetLayout> lensSetLayout_;
};

}