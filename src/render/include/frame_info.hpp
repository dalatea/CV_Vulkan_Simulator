#pragma once

#include "camera.hpp"
#include "object.hpp"

// lib

#include <vulkan/vulkan.h>

namespace enginev {

	#define MAX_LIGHTS 400

	struct BrightPushConstant {
		float threshold = 1.0f;
		float knee      = 0.5f;
		float pad0      = 0.0f;
		float pad1      = 0.0f;
	};

	struct ExposureDataBuffer {
		int32_t logLumSunScaled = 0.0f;
		int pixelCount = 0;
		float pad[2];
	};

	struct ExposureState {
		float autoExposure;
		float targetExposure;
		float adaptationRateUp;
		float adaptationRateDown;
		float dt;
	};

	struct BlurPushConstant {
		glm::vec2 texelSize{1.0f, 1.0f};
		float radius = 5.0f;           
		float pad0   = 0.0f;
	};
	static_assert(sizeof(BrightPushConstant) == 16, "BrightPushConstant size must match shader");


	struct PointLight {
		glm::vec4 position{};
		glm::vec4 color{};
	};

	struct GlobalUbo {
		alignas(16) glm::mat4 projection{ 1.f };
		alignas(16) glm::mat4 view{ 1.f };
		alignas(16) glm::mat4 inverseView{ 1.f };
		alignas(16) glm::mat4 lightViewProj{1.f};

		alignas(16) glm::vec4 ambientLightColor{ 1.f, 1.f, 1.f, 0.02f }; // w is intensity

		alignas(16) glm::vec4 sunDirection{0.f};
		alignas(16) glm::vec4 sunColor{1.f, 0.95f, 0.7f, 5.f};

		alignas(16) glm::vec4 sunParams{0.f, 0.f, 0.f, 0.f};
		alignas(16) glm::vec4 sunScreen{0.5f, 0.5f, 0.f, 1.f};

		alignas(16) PointLight pointLights[MAX_LIGHTS];
		alignas(16) int numLights{0};

		alignas(16) float autoExposure;

		alignas(16)  glm::vec3 _pad0{0.f};
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