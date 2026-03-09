#include "bright_render_system.hpp"

#include <stdexcept>
#include <cassert>

namespace enginev {

BrightExtractRenderSystem::BrightExtractRenderSystem(
    Device& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout)
    : device{device} {
    createPipelineLayout(setLayout);
    createPipeline(renderPass);
}

BrightExtractRenderSystem::~BrightExtractRenderSystem() {
    if (pipelineLayout) {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

void BrightExtractRenderSystem::createPipelineLayout(VkDescriptorSetLayout setLayout) {
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &setLayout;

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(BrightPushConstant); 

    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &push;

    if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bright extract pipeline layout");
    }
}

void BrightExtractRenderSystem::createPipeline(VkRenderPass renderPass) {
    assert(pipelineLayout);

    PipelineConfigInfo config{};
    Pipeline::defaultPipelineConfigInfo(config);

    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    config.renderPass = renderPass;
    config.pipelineLayout = pipelineLayout;

    pipeline = std::make_unique<Pipeline>(
        device,
        "../shaders/post.vert.spv",
        "../shaders/bright.frag.spv",
        config
    );
}

void BrightExtractRenderSystem::render(FrameInfo& frameInfo, VkDescriptorSet set, const BrightPushConstant& pc) {
    pipeline->bind(frameInfo.commandBuffer);

    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, 1, &set,
        0, nullptr
    );

    vkCmdPushConstants(
        frameInfo.commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(BrightPushConstant),
        &pc
    );
    vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
}

} // namespace enginev
