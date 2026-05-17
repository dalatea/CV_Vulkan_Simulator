#pragma once
#include "device.hpp"
#include "buffer.hpp"
#include "texture.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <vector>
#include <memory>
#include <string>
#include <cfloat>

namespace enginev {

class Model {
public:
    struct Vertex {
        glm::vec3 position{};
        glm::vec3 color{};
        glm::vec3 normal{};
        glm::vec3 tangent{0.0f};
        glm::vec2 uv{};

         glm::vec3 specularColor{1.f};
        float     shininess{32.f};

        static std::vector<VkVertexInputBindingDescription>   getBindingDescriptions();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

        bool operator==(const Vertex& other) const {
            return position == other.position && color == other.color &&
                   normal == other.normal && uv == other.uv;
        }
    };

    struct SubMesh {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int      materialId = -1;
        std::shared_ptr<Texture> texture{};      
        glm::vec3 diffuseColor{1.0f};           
        bool     hasTexture = false;
        std::shared_ptr<Texture> normalTexture;
        bool hasNormalMap = false;
        float alpha = 1.0f;
        bool  isTransparent = false;
    };

    struct MaterialInfo {
        std::string name;
        std::string diffuseTexturePath;     
        std::string normalTexturePath;     
        glm::vec3   diffuseColor{1.0f};
        glm::vec3   specularColor{0.0f};
        float       shininess{0.0f};
        float alpha = 1.0f; 
        int   illum = 2;
    };

    struct Builder {
        std::vector<Vertex>       vertices{};
        std::vector<uint32_t>     indices{};
        std::vector<SubMesh>      subMeshes{};
        std::vector<MaterialInfo> materials{};

        glm::vec3 bboxMin{};
        glm::vec3 bboxMax{};
        float     boundingRadius{};

         std::string diffuseTexturePath;

        void loadModel(const std::string& filepath);
        void computeTangents();
    };

    Model(Device& device, const Model::Builder& builder, float radius);
    ~Model();

    Model(const Model&)            = delete;
    Model& operator=(const Model&) = delete;

    static std::unique_ptr<Model> createModelFromFile(
        Device& device, const std::string& filepath);
    static std::shared_ptr<Model> createSkyboxCube(Device& device);

    void bind(VkCommandBuffer commandBuffer);

    void draw(VkCommandBuffer commandBuffer);

    void drawSubMesh(VkCommandBuffer commandBuffer, size_t subMeshIndex);

    const std::vector<SubMesh>&      getSubMeshes() const { return subMeshes_; }
    const std::vector<MaterialInfo>& getMaterials() const { return materials_; }

    // Legacy single-texture accessors (kept for backward compat; may be null).
    std::shared_ptr<Texture> getTexture() const       { return texture; }
    void setTexture(std::shared_ptr<Texture> tex)     { texture = tex; }

    std::string mtlDiffuseTexture;

    float boundingRadius = 1.0f;

    VkAccelerationStructureKHR blas       = VK_NULL_HANDLE;
    VkDeviceMemory             blasMemory = VK_NULL_HANDLE;
    VkBuffer                   blasBuffer = VK_NULL_HANDLE;
    void buildBlas(Device& device);
    void destroyBlas(Device& device);

private:
    void createVertexBuffers(const std::vector<Vertex>& vertices);
    void createIndexBuffers(const std::vector<uint32_t>& indices);
    void createSubMeshTextures(const Builder& builder);

    Device& device;

    std::unique_ptr<Buffer> vertexBuffer;
    uint32_t vertexCount = 0;

    std::unique_ptr<Buffer> indexBuffer;
    uint32_t indexCount = 0;
    bool     hasIndexBuffer = false;

    std::vector<SubMesh>      subMeshes_;
    std::vector<MaterialInfo> materials_;

    std::shared_ptr<Texture> texture;
};

} // namespace enginev