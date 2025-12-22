#pragma once

#include "device.hpp"
#include "pipeline.hpp"
#include "frame_info.hpp"
#include "descriptors.hpp"

#include <memory>

namespace enginev {

class PostProcessRenderSystem {
public:
    PostProcessRenderSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout);
    ~PostProcessRenderSystem();

    PostProcessRenderSystem(const PostProcessRenderSystem&) = delete;
    PostProcessRenderSystem& operator=(const PostProcessRenderSystem&) = delete;

    void render(FrameInfo& frameInfo, VkDescriptorSet postSet);

private:
    void createPipelineLayout(VkDescriptorSetLayout setLayout);
    void createPipeline(VkRenderPass renderPass);

    Device& device;
    std::unique_ptr<Pipeline> pipeline;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

}
