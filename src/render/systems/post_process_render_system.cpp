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
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &setLayout;
    info.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device.device(), &info, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create postprocess pipeline layout");
    }
}

void PostProcessRenderSystem::createPipeline(VkRenderPass renderPass) {
    assert(pipelineLayout && "pipeline layout must exist");

    PipelineConfigInfo config{};
    Pipeline::defaultPipelineConfigInfo(config);

    // fullscreen triangle: без вершинных входов
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();

    config.renderPass = renderPass;
    config.pipelineLayout = pipelineLayout;

    // depth не нужен
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    pipeline = std::make_unique<Pipeline>(
        device,
        "../shaders/post.vert.spv",
        "../shaders/post.frag.spv",
        config
    );
}

void PostProcessRenderSystem::render(FrameInfo& frameInfo, VkDescriptorSet postSet) {
    pipeline->bind(frameInfo.commandBuffer);

    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, 1, &postSet,
        0, nullptr
    );

    // fullscreen triangle
    vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
}

}