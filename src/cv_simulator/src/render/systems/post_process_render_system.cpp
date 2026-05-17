#include "post_process_render_system.hpp"

#include <stdexcept>
#include <array>
#include <cassert>

namespace enginev {

PostProcessRenderSystem::PostProcessRenderSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout)
    : device{device} {
    createPipelineLayout(setLayout);
    createPipeline(renderPass);
}

PostProcessRenderSystem::~PostProcessRenderSystem() {
    if (pipelineLayout) {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

void PostProcessRenderSystem::createPipelineLayout(VkDescriptorSetLayout setLayout) {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PostProcessPushConstant);

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &setLayout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create postprocess pipeline layout");
    }
}

void PostProcessRenderSystem::createPipeline(VkRenderPass renderPass) {
    assert(pipelineLayout && "pipeline layout must exist");

    PipelineConfigInfo config{};
    Pipeline::defaultPipelineConfigInfo(config);

    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();

    config.renderPass = renderPass;
    config.pipelineLayout = pipelineLayout;

    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    pipeline = std::make_unique<Pipeline>(
        device,
        "post.vert.spv",
        "post.frag.spv",
        config
    );
}

void PostProcessRenderSystem::render(FrameInfo& frameInfo, VkDescriptorSet postSet, const PostProcessPushConstant& pc) {
    pipeline->bind(frameInfo.commandBuffer);

    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, 1, &postSet,
        0, nullptr
    );

    vkCmdPushConstants(
        frameInfo.commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PostProcessPushConstant),
        &pc
    );

    vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
}

}