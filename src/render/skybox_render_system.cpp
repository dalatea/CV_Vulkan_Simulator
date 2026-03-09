#include "skybox_render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// std
#include <cassert>
#include <stdexcept>

namespace enginev {
    SkyboxRenderSystem::SkyboxRenderSystem(
        Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
        : device{ device } {
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);

        skyboxModel = Model::createModelFromFile(device, "../models/cube.obj");
    }

    SkyboxRenderSystem::~SkyboxRenderSystem() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void SkyboxRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        std::vector<VkDescriptorSetLayout> setLayouts{ globalSetLayout };
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void SkyboxRenderSystem::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

        PipelineConfigInfo pipelineConfig{};
        Pipeline::defaultPipelineConfigInfo(pipelineConfig);

        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;

        pipelineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
        pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
        pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        pipelineConfig.bindingDescriptions.clear();
        pipelineConfig.attributeDescriptions.clear();

        pipelineConfig.bindingDescriptions = Model::Vertex::getBindingDescriptions();
        pipelineConfig.attributeDescriptions = Model::Vertex::getAttributeDescriptions();
        /*
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(glm::vec3);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrPos{};
        attrPos.location = 0;
        attrPos.binding = 0;
        attrPos.format = VK_FORMAT_R32G32B32_SFLOAT;
        attrPos.offset = 0;

        pipelineConfig.bindingDescriptions.push_back(binding);
        pipelineConfig.attributeDescriptions.push_back(attrPos);*/

        pipeline = std::make_unique<Pipeline>(
            device,
            "../shaders/skybox.vert.spv",
            "../shaders/skybox.frag.spv",
            pipelineConfig);
    }

    void SkyboxRenderSystem::render(FrameInfo& frameInfo) {
       
        pipeline->bind(frameInfo.commandBuffer);

        vkCmdBindDescriptorSets(
            frameInfo.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            1,
            &frameInfo.globalDescriptorSet,
            0,
            nullptr);
        
        skyboxModel->bind(frameInfo.commandBuffer);
        skyboxModel->draw(frameInfo.commandBuffer);
    }

}
