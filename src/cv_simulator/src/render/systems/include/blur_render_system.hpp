#pragma once

#include "device.hpp"
#include "pipeline.hpp"
#include "frame_info.hpp"

#include <memory>

namespace enginev {

class BlurRenderSystem {
public:
    BlurRenderSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout, bool horizontal);
    ~BlurRenderSystem();

    BlurRenderSystem(const BlurRenderSystem&) = delete;
    BlurRenderSystem& operator=(const BlurRenderSystem&) = delete;

    void render(FrameInfo& frameInfo, VkDescriptorSet set, const BlurPushConstant& pc);

private:
    void createPipelineLayout(VkDescriptorSetLayout setLayout);
    void createPipeline(VkRenderPass renderPass, bool horizontal);

private:
    Device& device;
    std::unique_ptr<Pipeline> pipeline;
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    bool horizontal = true;
};

} // namespace enginev