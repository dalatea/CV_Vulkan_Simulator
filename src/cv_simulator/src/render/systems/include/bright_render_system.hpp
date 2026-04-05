#pragma once

#include "device.hpp"
#include "pipeline.hpp"
#include "frame_info.hpp"

#include <memory>

namespace enginev {

class BrightExtractRenderSystem {
public:
    BrightExtractRenderSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout);
    ~BrightExtractRenderSystem();

    BrightExtractRenderSystem(const BrightExtractRenderSystem&) = delete;
    BrightExtractRenderSystem& operator=(const BrightExtractRenderSystem&) = delete;

    void render(FrameInfo& frameInfo, VkDescriptorSet set, const BrightPushConstant& pc);

private:
    void createPipelineLayout(VkDescriptorSetLayout setLayout);
    void createPipeline(VkRenderPass renderPass);

private:
    Device& device;
    std::unique_ptr<Pipeline> pipeline;
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
};

} // namespace enginev