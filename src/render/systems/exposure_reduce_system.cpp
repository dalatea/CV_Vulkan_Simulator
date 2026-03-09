#include "exposure_reduce_system.hpp"
#include "pipeline.hpp"
#include <vulkan/vulkan.h>
#include <array>

namespace enginev {

     static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("failed to open file: " + filename);

        size_t size = (size_t)file.tellg();
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), size);
        file.close();
        return buffer;
    }

    static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode    = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule module;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("failed to create shader module");

        return module;
    }

    ExposureReduceSystem::ExposureReduceSystem(Device& device)
        : device(device)
    {
        createDescriptorSetLayout();
        createPipelineLayout();
        createPipeline();
    }

    ExposureReduceSystem::~ExposureReduceSystem() {
        vkDestroyPipeline(device.device(), pipeline, nullptr);
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device.device(), descriptorSetLayout, nullptr);
    }

    void ExposureReduceSystem::createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding hdrBinding{};
        hdrBinding.binding = 0;
        hdrBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        hdrBinding.descriptorCount = 1;
        hdrBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding exposureBinding{};
        exposureBinding.binding = 1;
        exposureBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        exposureBinding.descriptorCount = 1;
        exposureBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            hdrBinding, exposureBinding
        };

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = (uint32_t)bindings.size();
        info.pBindings = bindings.data();

        vkCreateDescriptorSetLayout(device.device(), &info, nullptr, &descriptorSetLayout);
    }

    void ExposureReduceSystem::createPipelineLayout() {
        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = 1;
        info.pSetLayouts = &descriptorSetLayout;

        vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout);
    }

    void ExposureReduceSystem::createPipeline() {

        auto code = readFile("../shaders/exposure_reduce.comp.spv");
        VkShaderModule shaderModule = createShaderModule(device.device(), code);

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shaderModule;
        stage.pName = "main";

        VkComputePipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage = stage;
        info.layout = pipelineLayout;

        if (vkCreateComputePipelines(device.device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("failed to create exposure reduce pipeline");

        vkDestroyShaderModule(device.device(), shaderModule, nullptr);
    }

    void ExposureReduceSystem::dispatch(VkCommandBuffer cmd, VkExtent2D size, VkDescriptorSet hdrSet) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout, 0, 1, &hdrSet, 0, nullptr);

        uint32_t gx = (size.width + 15) / 16;
        uint32_t gy = (size.height + 15) / 16;

        vkCmdDispatch(cmd, gx, gy, 1);
    }

}
