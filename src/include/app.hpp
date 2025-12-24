#pragma once

#include "window.hpp"
#include "object.hpp"
#include "renderer.hpp"
#include "device.hpp"
#include "descriptors.hpp"
#include "camera.hpp"
#include "scene_pass.hpp"
#include "bloom_pass.hpp"
#include "lens_flare_pass.hpp"

#include <unordered_map>
#include <string>
#include <array> 
#include <memory>
#include <vector>

using namespace enginev;

namespace cvsim {

	struct StressConfig {
		bool enabled = false;
		int count = 50000;
		float spacing = 2.0f;
		std::string modelPath; // optional

		std::string scenePath = "../assets/scene_config.json";
	};

	enum class CameraControlType { Keyboard, ROS };

	struct CameraRig {
		CameraRig() :
			rig(enginev::SimObject::createSimObject()) {};
		enginev::Camera camera{};
		enginev::SimObject rig;
		CameraControlType control = CameraControlType::Keyboard;

		float yawSpeed = 1.0f;
		float pitchSpeed = 1.0f;
		float moveSpeed = 1.0f;

		static CameraRig MakeCam(
			const glm::vec3& pos,
			CameraControlType camera_type, 
			const glm::vec3& rot = {0.f,0.f,0.f}) {
			CameraRig r;
			r.control = camera_type;
			r.rig.transform.translation = pos;
			r.rig.transform.rotation = rot;
			return r;
		}

		template <typename RosCmdT>
		void applyRos(float dt, const RosCmdT& cmd)
    	{
			rig.transform.rotation.y += yawSpeed   * cmd.angular.z * dt;
			rig.transform.rotation.x -= pitchSpeed * cmd.angular.y * dt;

			rig.transform.rotation.y = glm::mod(rig.transform.rotation.y, glm::two_pi<float>());
			rig.transform.rotation.x =
				glm::clamp(rig.transform.rotation.x, -1.5f, 1.5f);
			rig.transform.rotation.z = 0.0f;

			float yaw = rig.transform.rotation.y;
			const glm::vec3 forwardDir{ sin(yaw), 0.f, cos(yaw) };
			const glm::vec3 rightDir{ forwardDir.z, 0.f, -forwardDir.x };
			const glm::vec3 upDir{ 0.f, 1.f, 0.f };
	
			float lx = static_cast<float>(cmd.linear.x);
			float ly = static_cast<float>(cmd.linear.y);
			float lz = static_cast<float>(cmd.linear.z);

			glm::vec3 moveDir =
				forwardDir * lx +
				rightDir * ly +
				upDir * lz;

			if (glm::length(moveDir) > std::numeric_limits<float>::epsilon()) {
				rig.transform.translation += moveSpeed * dt * moveDir;
			}
		}
	};

	struct FrameCapture {
		VkBuffer buf{ VK_NULL_HANDLE };
		VkDeviceMemory mem{ VK_NULL_HANDLE };
		void* mapped{ nullptr };
		size_t size{ 0 };
	};

	class SimApp {
	public:
	    const uint32_t SHADOW_MAP_WIDTH  = 4096;
        const uint32_t SHADOW_MAP_HEIGHT = 4096;
		
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 600;

		SimApp();
		explicit SimApp(const StressConfig& stressCfg);
		~SimApp();

		SimApp(const SimApp&) = delete;
		SimApp& operator=(const SimApp&) = delete;

		void run();
	
	private:

		StressConfig stressCfg_{};

		void loadSimObjects();

		Window window{ WIDTH, HEIGHT, "CV Sim!" };
		Device device{ window };
		Renderer renderer{ window, device };

		std::unique_ptr<ScenePass> scenePass;
		std::unique_ptr<BloomPass> bloomPass;
		std::vector<VkDescriptorSet> postDescriptorSets;
		std::unique_ptr<DescriptorSetLayout> postSetLayout;
		std::unique_ptr<LensFlarePass> lensFlarePass;
		std::vector<LensSurfaceGPU> lenSurfacesCpu;
		glm::vec3 lightDir{0.0f};
		glm::vec4 sunColor{1.f, 0.95f, 0.7f, 1.f};
		std::unordered_map<std::string, std::shared_ptr<Model>> modelCache_;
		std::shared_ptr<Model> getModelCached_(const std::string& modelPath);

		std::unique_ptr<DescriptorPool> globalPool{};
		std::array<FrameCapture, SwapChain::MAX_FRAMES_IN_FLIGHT> captures;
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