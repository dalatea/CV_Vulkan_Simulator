#pragma once
#include "device.hpp"

namespace enginev {

    class ExposureUpdateSystem {
    public:
        ExposureUpdateSystem(Device& device);
        ~ExposureUpdateSystem();

        void dispatch(VkCommandBuffer cmd, VkDescriptorSet exposureSet);

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
