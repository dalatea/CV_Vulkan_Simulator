#pragma once

#include "device.hpp"
#include "object.hpp"
#include "pipeline.hpp"
#include "camera.hpp"
#include "frame_info.hpp"

// std
#include <memory>
#include <vector>

namespace enginev {
	class SimpleRenderSystem {
	public:
		SimpleRenderSystem(
			Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
		~SimpleRenderSystem();

		SimpleRenderSystem(const SimpleRenderSystem&) = delete;
		SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;

		void renderSimObjects(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
		void createPipeline(VkRenderPass renderPass);

		Device& device;

		std::unique_ptr<Pipeline> pipeline;
		VkPipelineLayout pipelineLayout;
	};
}