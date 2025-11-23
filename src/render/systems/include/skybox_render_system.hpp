#pragma once

#include "device.hpp"
#include "object.hpp"
#include "pipeline.hpp"
#include "frame_info.hpp"

// std
#include <memory>

namespace enginev {
	class SkyboxRenderSystem {
	public:
		SkyboxRenderSystem(
			Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
		~SkyboxRenderSystem();

		SkyboxRenderSystem(const SkyboxRenderSystem&) = delete;
		SkyboxRenderSystem& operator=(const SkyboxRenderSystem&) = delete;

		void render(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
		void createPipeline(VkRenderPass renderPass);

		Device& device;

		std::unique_ptr<Pipeline> pipeline;
		VkPipelineLayout pipelineLayout;

        std::shared_ptr<Model> skyboxModel;
	};
}