#include "simple_render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/component_wise.hpp>

// std
#include <array>
#include <cassert>
#include <stdexcept>

namespace enginev {

    struct SimplePushConstantData {
        glm::mat4 modelMatrix{ 1.f };
        glm::mat4 normalMatrix{ 1.f };
    };

    SimpleRenderSystem::SimpleRenderSystem(
        Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
        : device{ device } {
        createPerObjectDescriptorSetLayout();
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
        createDefaultTexture();
    }

    SimpleRenderSystem::~SimpleRenderSystem() {
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }

    void SimpleRenderSystem::createPerObjectDescriptorSetLayout() {
        perObjectSetLayout = DescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

        perObjectPool = DescriptorPool::Builder(device)
            .setMaxSets(1000)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000)
            .build();
    }


    void SimpleRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SimplePushConstantData);

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
            globalSetLayout,
            perObjectSetLayout->getDescriptorSetLayout()  
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        
        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    VkDescriptorSet SimpleRenderSystem::getOrCreateDescriptorSet(SimObject& obj) {
        auto it = perObjectSets.find(obj.getId());
        if (it != perObjectSets.end()) return it->second;

        Texture* tex = obj.texture ? obj.texture.get() : defaultTexture.get();
        VkDescriptorImageInfo imageInfo = tex->getDescriptorInfo();

        VkDescriptorSet set;
        DescriptorWriter(*perObjectSetLayout, *perObjectPool)
            .writeImage(0, &imageInfo)
            .build(set);

        perObjectSets[obj.getId()] = set;
        return set;
    }
    void SimpleRenderSystem::createPipeline(VkRenderPass renderPass) {
        assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

        PipelineConfigInfo pipelineConfig{};
        Pipeline::defaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;
        pipelineConfig.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        pipeline = std::make_unique<Pipeline>(
            device,
            "shader.vert.spv",
            "shader.frag.spv",
            pipelineConfig);
    }

    void SimpleRenderSystem::renderSimObjects(FrameInfo& frameInfo) {
       
        pipeline->bind(frameInfo.commandBuffer);

        vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &frameInfo.globalDescriptorSet, 0, nullptr);


        for (auto& kv : frameInfo.simObjects) {
            auto& obj = kv.second;
            if (obj.model == nullptr) continue;

            glm::vec3 worldPos = obj.transform.translation;

            float scaledRadius =
                obj.model->boundingRadius *
                glm::compMax(obj.transform.scale);
            
            if (!isVisible(frameInfo.frustum, worldPos, scaledRadius))
                continue;

            VkDescriptorSet perObjSet = getOrCreateDescriptorSet(obj);
            vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, 1, 1, &perObjSet, 0, nullptr);
                
            SimplePushConstantData push{};
            push.modelMatrix = obj.transform.mat4();
            push.normalMatrix = obj.transform.normalMatrix();

            vkCmdPushConstants(
                frameInfo.commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(SimplePushConstantData),
                &push);
            obj.model->bind(frameInfo.commandBuffer);
            obj.model->draw(frameInfo.commandBuffer);
        }
    }

    void SimpleRenderSystem::createDefaultTexture() {
        uint8_t whitePixel[4] = {255, 255, 255, 255};

        defaultTexture = std::make_unique<Texture>(device, whitePixel, 1, 1);
    }

}
