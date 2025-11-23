#pragma once

#include "camera.hpp"
#include "object.hpp"

// lib

#include <vulkan/vulkan.h>

namespace enginev {

	#define MAX_LIGHTS 10

	struct PointLight {
		glm::vec4 position{};
		glm::vec4 color{};
	};

	struct GlobalUbo {
		glm::mat4 projection{ 1.f };
		glm::mat4 view{ 1.f };
		glm::mat4 inverseView{ 1.f };
		glm::mat4 lightViewProj{1.f};

		glm::vec4 ambientLightColor{ 1.f, 1.f, 1.f, 0.02f }; // w is intensity

		glm::vec4 sunDirection{0.f};
		glm::vec4 sunColor{1.f, 0.95f, 0.7f, 5.f};

		PointLight pointLights[MAX_LIGHTS];
		int numLights{0};
	};

	struct FrameInfo {
		int frameIndex;
		float frameTime;
		VkCommandBuffer commandBuffer;
		Camera& camera;
		VkDescriptorSet globalDescriptorSet;
		SimObject::Map &simObjects;
	};
}