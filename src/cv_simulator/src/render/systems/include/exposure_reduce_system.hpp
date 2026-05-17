#pragma once
#include "device.hpp"
#include "frame_info.hpp"

#include <vulkan/vulkan.h>
#include <fstream>
#include <array>

namespace enginev {

    class ExposureReduceSystem {
    public:
        ExposureReduceSystem(Device& device);
        ~ExposureReduceSystem();

        void dispatch(VkCommandBuffer cmd, VkExtent2D size, VkDescriptorSet hdrSet);

        VkDescriptorSetLayout getDescriptorSetLayout() { return descriptorSetLayout; }
        VkPipelineLayout getPipelineLayout() { return pipelineLayout; }

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createPipeline();
        

        Device& device;

        VkDescriptorSetLayout descriptorSetLayout{};
        VkPipelineLayout pipelineLayout{};
        VkPipeline pipeline{};
    };

}
