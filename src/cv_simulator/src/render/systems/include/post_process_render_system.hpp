#pragma once

#include "device.hpp"
#include "pipeline.hpp"
#include "frame_info.hpp"
#include "descriptors.hpp"

#include <memory>

namespace enginev {

    struct PostProcessPushConstant {
        float dustDensity    = 0.0f;
        float smudgeAmount   = 0.0f;
        float scratchAmount  = 0.0f;
        float waterDroplets  = 0.0f;
        float scatterFactor  = 0.0f;
        float pad0 = 0.0f, pad1 = 0.0f, pad2 = 0.0f;
    };
    class PostProcessRenderSystem {
    public:
        PostProcessRenderSystem(Device& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout);
        ~PostProcessRenderSystem();

        PostProcessRenderSystem(const PostProcessRenderSystem&) = delete;
        PostProcessRenderSystem& operator=(const PostProcessRenderSystem&) = delete;

        void render(FrameInfo& frameInfo, VkDescriptorSet postSet,
            const PostProcessPushConstant& pc = {});
    private:
        void createPipelineLayout(VkDescriptorSetLayout setLayout);
        void createPipeline(VkRenderPass renderPass);

        Device& device;
        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

}
