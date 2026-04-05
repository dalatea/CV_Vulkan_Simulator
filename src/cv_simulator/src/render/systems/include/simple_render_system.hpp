#pragma once

#include "device.hpp"
#include "object.hpp"
#include "pipeline.hpp"
#include "camera.hpp"
#include "frame_info.hpp"
#include "texture.hpp"
#include "descriptors.hpp"

// std
#include <memory>
#include <vector>
#include <unordered_map>

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
		void createPerObjectDescriptorSetLayout();
		void createDefaultTexture();
		VkDescriptorSet getOrCreateDescriptorSet(SimObject& obj); 

		Device& device;
		std::unique_ptr<Pipeline> pipeline;
    	VkPipelineLayout pipelineLayout;

		std::unique_ptr<DescriptorSetLayout> perObjectSetLayout;
		std::unique_ptr<DescriptorPool> perObjectPool;
		std::unordered_map<unsigned int, VkDescriptorSet> perObjectSets;
		std::unique_ptr<Texture> defaultTexture;
	};
}