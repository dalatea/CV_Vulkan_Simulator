#pragma once

#include "device.hpp"
#include "object.hpp"
#include "pipeline.hpp"
#include "camera.hpp"
#include "frame_info.hpp"
#include "texture.hpp"
#include "descriptors.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace enginev {

class SimpleRenderSystem {
public:
    SimpleRenderSystem(Device& device, VkRenderPass renderPass,
                       VkDescriptorSetLayout globalSetLayout);
    ~SimpleRenderSystem();

    SimpleRenderSystem(const SimpleRenderSystem&)            = delete;
    SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;

    void renderSimObjects(FrameInfo& frameInfo);

private:
    void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void createPipeline(VkRenderPass renderPass);
    void createPerObjectDescriptorSetLayout();
    void createDefaultTexture();
     void createTransparentPipeline(VkRenderPass renderPass);

    struct PerObjSubKey {
        unsigned int objId;
        uint32_t     subMeshIdx;
        bool operator==(const PerObjSubKey& o) const {
            return objId == o.objId && subMeshIdx == o.subMeshIdx;
        }
    };
    struct PerObjSubKeyHash {
        size_t operator()(const PerObjSubKey& k) const noexcept {
            return std::hash<unsigned int>()(k.objId) ^
                   (std::hash<uint32_t>()(k.subMeshIdx) << 1);
        }
    };

    VkDescriptorSet getOrCreateDescriptorSet(SimObject& obj,
                                             size_t     subMeshIdx,
                                             Texture*   subTex);

    Device& device;
    std::unique_ptr<Pipeline> pipeline;
    std::unique_ptr<Pipeline> pipelineTransparent;
    VkPipelineLayout          pipelineLayout;

    std::unique_ptr<DescriptorSetLayout> perObjectSetLayout;
    std::unique_ptr<DescriptorPool>      perObjectPool;

    std::unique_ptr<Texture> defaultTexture; 
    std::unique_ptr<Texture> defaultNormalTexture; 

    std::unordered_map<PerObjSubKey, VkDescriptorSet, PerObjSubKeyHash> perObjectSets;
};

} // namespace enginev