#include "simple_render_system.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/component_wise.hpp>

#include <array>
#include <cassert>
#include <stdexcept>

namespace enginev {

struct SimplePushConstantData {
    glm::mat4 modelMatrix{ 1.f };
    glm::mat4 normalMatrix{ 1.f };
    glm::vec4 materialParams{ 1.f, 0.f, 0.f, 0.f };
};

SimpleRenderSystem::SimpleRenderSystem(
    Device& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : device{ device } {
    createPerObjectDescriptorSetLayout();
    createPipelineLayout(globalSetLayout);
    createPipeline(renderPass);
    createTransparentPipeline(renderPass);
    createDefaultTexture();
}

SimpleRenderSystem::~SimpleRenderSystem() {
    vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
}

void SimpleRenderSystem::createPerObjectDescriptorSetLayout() {
    perObjectSetLayout = DescriptorSetLayout::Builder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

       perObjectPool = DescriptorPool::Builder(device)
        .setMaxSets(8000)
        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16000)
        .build();
}

void SimpleRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size   = sizeof(SimplePushConstantData);

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
        globalSetLayout,
        perObjectSetLayout->getDescriptorSetLayout()
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount         = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts            = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges    = &pushConstantRange;

    if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

VkDescriptorSet SimpleRenderSystem::getOrCreateDescriptorSet(
    SimObject& obj, size_t subMeshIdx, Texture* subTex) {
    PerObjSubKey key{ obj.getId(), static_cast<uint32_t>(subMeshIdx) };
    auto it = perObjectSets.find(key);
    if (it != perObjectSets.end()) return it->second;

    Texture* diff = nullptr;
    if (obj.texture)      diff = obj.texture.get();
    else if (subTex)      diff = subTex;
    else                  diff = defaultTexture.get();

    Texture* norm = defaultNormalTexture.get();
    if (obj.model) {
        const auto& subs = obj.model->getSubMeshes();
        if (subMeshIdx < subs.size() && subs[subMeshIdx].normalTexture) {
            norm = subs[subMeshIdx].normalTexture.get();
        }
    }

    VkDescriptorImageInfo diffuseInfo = diff->getDescriptorInfo();
    VkDescriptorImageInfo normalInfo  = norm->getDescriptorInfo();

    VkDescriptorSet set;
    DescriptorWriter(*perObjectSetLayout, *perObjectPool)
        .writeImage(0, &diffuseInfo)
        .writeImage(1, &normalInfo)
        .build(set);

    perObjectSets[key] = set;
    return set;
}

void SimpleRenderSystem::createPipeline(VkRenderPass renderPass) {
    assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    PipelineConfigInfo pipelineConfig{};
    Pipeline::defaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass     = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout;
    pipelineConfig.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    pipelineConfig.rasterizationInfo.cullMode  = VK_CULL_MODE_NONE; 
    
    pipeline = std::make_unique<Pipeline>(
        device,
        "shader.vert.spv",
        "shader.frag.spv",
        pipelineConfig);
}

void SimpleRenderSystem::createTransparentPipeline(VkRenderPass renderPass) {
    assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    PipelineConfigInfo cfg{};
    Pipeline::defaultPipelineConfigInfo(cfg);
    cfg.renderPass     = renderPass;
    cfg.pipelineLayout = pipelineLayout;
    cfg.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    cfg.rasterizationInfo.cullMode  = VK_CULL_MODE_NONE;

    // Alpha blending
    cfg.colorBlendAttachment.blendEnable         = VK_TRUE;
    cfg.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cfg.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cfg.colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    cfg.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cfg.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cfg.colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    // Depth test ON, depth write OFF
    cfg.depthStencilInfo.depthTestEnable  = VK_TRUE;
    cfg.depthStencilInfo.depthWriteEnable = VK_FALSE;

    pipelineTransparent = std::make_unique<Pipeline>(
        device, "shader.vert.spv", "shader.frag.spv", cfg);
}

void SimpleRenderSystem::renderSimObjects(FrameInfo& frameInfo) {
    auto drawPass = [&](bool transparentPass) {
        Pipeline* pl = transparentPass ? pipelineTransparent.get() : pipeline.get();
        pl->bind(frameInfo.commandBuffer);

        vkCmdBindDescriptorSets(frameInfo.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
            &frameInfo.globalDescriptorSet, 0, nullptr);

        for (auto& kv : frameInfo.simObjects) {
            auto& obj = kv.second;
            if (obj.model == nullptr) continue;

            glm::vec3 worldPos = obj.transform.translation;
            float scaledRadius = obj.model->boundingRadius *
                                 glm::compMax(obj.transform.scale);
            if (!isVisible(frameInfo.frustum, worldPos, scaledRadius)) continue;

            obj.model->bind(frameInfo.commandBuffer);
            const auto& subs = obj.model->getSubMeshes();

            if (subs.empty()) {
                if (transparentPass) continue; // нет submesh — считаем opaque

                SimplePushConstantData push{};
                push.modelMatrix    = obj.transform.mat4();
                push.normalMatrix   = obj.transform.normalMatrix();
                push.materialParams = glm::vec4(1.f, 0.f, 0.f, 0.f);

                vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(push), &push);

                VkDescriptorSet perObjSet = getOrCreateDescriptorSet(obj, 0, nullptr);
                vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                    1, 1, &perObjSet, 0, nullptr);
                obj.model->draw(frameInfo.commandBuffer);
                continue;
            }

            for (size_t i = 0; i < subs.size(); ++i) {
                if (subs[i].isTransparent != transparentPass) continue;

                SimplePushConstantData push{};
                push.modelMatrix    = obj.transform.mat4();
                push.normalMatrix   = obj.transform.normalMatrix();
                push.materialParams = glm::vec4(subs[i].alpha, 0.f, 0.f, 0.f);

                vkCmdPushConstants(frameInfo.commandBuffer, pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(push), &push);

                Texture* subTex = subs[i].texture ? subs[i].texture.get() : nullptr;
                VkDescriptorSet perObjSet = getOrCreateDescriptorSet(obj, i, subTex);
                vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                    1, 1, &perObjSet, 0, nullptr);

                obj.model->drawSubMesh(frameInfo.commandBuffer, i);
            }
        }
    };

    drawPass(false); 
    drawPass(true); 
}

void SimpleRenderSystem::createDefaultTexture() {
    uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    defaultTexture = std::make_unique<Texture>(device, whitePixel, 1, 1, /*isSRGB=*/true);

    uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 };
    defaultNormalTexture = std::make_unique<Texture>(
        device, flatNormalPixel, 1, 1, /*isSRGB=*/false);
}

} // namespace enginev