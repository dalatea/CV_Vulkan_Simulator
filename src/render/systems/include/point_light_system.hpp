#pragma once

#include "camera.hpp"
#include "device.hpp"
#include "frame_info.hpp"
#include "object.hpp"
#include "pipeline.hpp"

// std
#include <memory>
#include <vector>

namespace enginev {
    class PointLightSystem {
    public:
        PointLightSystem(
            Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
        ~PointLightSystem();

        PointLightSystem(const PointLightSystem&) = delete;
        PointLightSystem& operator=(const PointLightSystem&) = delete;

        void update(FrameInfo& frameInfo, GlobalUbo& ubo);
        void render(FrameInfo& frameInfo);

    private:
        void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
        void createPipeline(VkRenderPass renderPass);

        Device& device;

        std::unique_ptr<Pipeline> pipeline;
        VkPipelineLayout pipelineLayout;
    };
}  // namespace lve