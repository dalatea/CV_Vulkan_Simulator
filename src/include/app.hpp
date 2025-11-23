#pragma once

#include "window.hpp"
#include "object.hpp"
#include "renderer.hpp"
#include "device.hpp"
#include "descriptors.hpp"

#include <array> 
#include <memory>
#include <vector>

using namespace enginev;

namespace cvsim {

	struct FrameCapture {
		VkBuffer buf{ VK_NULL_HANDLE };
		VkDeviceMemory mem{ VK_NULL_HANDLE };
		void* mapped{ nullptr };
		size_t size{ 0 };
	};

	class SimApp {
	public:
	    const uint32_t SHADOW_MAP_WIDTH  = 2048;
        const uint32_t SHADOW_MAP_HEIGHT = 2048;
		
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 600;

		SimApp();
		~SimApp();

		SimApp(const SimApp&) = delete;
		SimApp& operator=(const SimApp&) = delete;

		void run();
	
	private:
		//void createScene();
		void loadSimObjects();

		Window window{ WIDTH, HEIGHT, "CV Sim!" };
		Device device{ window };
		Renderer renderer{ window, device };
		glm::vec3 lightDir{0.0f};
		glm::vec4 sunColor{1.f, 0.95f, 0.7f, 1.f};
		glm::vec3 sunWorldPos{0.0f, -1.0f, 0.0f};


		std::unique_ptr<DescriptorPool> globalPool{};
		std::array<FrameCapture, enginev::SwapChain::MAX_FRAMES_IN_FLIGHT> captures;
		SimObject::Map simObjects;

		VkImage shadowImage{VK_NULL_HANDLE};
		VkDeviceMemory shadowImageMemory{VK_NULL_HANDLE};
		VkImageView shadowImageView{VK_NULL_HANDLE};
		VkSampler shadowSampler{VK_NULL_HANDLE};

		VkRenderPass shadowRenderPass{VK_NULL_HANDLE};
		VkFramebuffer shadowFramebuffer{VK_NULL_HANDLE};
		VkExtent2D shadowExtent{2048, 2048};

		VkImage skyboxImage{VK_NULL_HANDLE};
		VkDeviceMemory skyboxImageMemory{VK_NULL_HANDLE};
		VkImageView skyboxImageView{VK_NULL_HANDLE};
		VkSampler skyboxSampler{VK_NULL_HANDLE};

		void createShadowResources();
		void destroyShadowResources();

		void createShadowRenderPass(VkFormat depthFormat);
		void createShadowFramebuffer();

		void createSkyboxCubemap();
		void destroySkyboxCubemap();
	};
}