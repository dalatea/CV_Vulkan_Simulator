#include "model.hpp"
#include "utils.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace std {
template <>
struct hash<enginev::Model::Vertex> {
    size_t operator()(enginev::Model::Vertex const& vertex) const {
        size_t seed = 0;
        enginev::hashCombine(seed, vertex.position, vertex.color,
                             vertex.normal, vertex.uv);
        return seed;
    }
};
} // namespace std

namespace enginev {

// ------------------------------------------------------------------
// Model
// ------------------------------------------------------------------

Model::Model(Device& device, const Model::Builder& builder, float radius)
    : boundingRadius(radius),
      device(device),
      subMeshes_(builder.subMeshes),
      materials_(builder.materials) {
    createVertexBuffers(builder.vertices);
    createIndexBuffers(builder.indices);
    createSubMeshTextures(builder);
}

Model::~Model() {}

std::unique_ptr<Model> Model::createModelFromFile(
    Device& device, const std::string& filepath) {
    Builder builder{};
    builder.loadModel(filepath);

    auto model = std::make_unique<Model>(device, builder, builder.boundingRadius);
    model->mtlDiffuseTexture = builder.diffuseTexturePath;
    return model;
}

std::shared_ptr<Model> Model::createSkyboxCube(Device& device) {
    static const std::vector<glm::vec3> CUBE_POSITIONS = {
        {-1, -1, -1}, {1, -1, -1}, {1,  1, -1}, {-1,  1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1,  1,  1}, {-1,  1,  1}
    };
    static const std::vector<uint32_t> CUBE_INDICES = {
        0,1,2, 2,3,0,
        4,5,6, 6,7,4,
        0,4,7, 7,3,0,
        1,5,6, 6,2,1,
        3,2,6, 6,7,3,
        0,1,5, 5,4,0
    };

    Builder builder{};
    builder.vertices.reserve(CUBE_POSITIONS.size());
    for (auto& p : CUBE_POSITIONS) {
        Vertex v{};
        v.position = p;
        v.color    = {1, 1, 1};
        v.normal   = {0, 0, 0};
        v.uv       = {0, 0};
        builder.vertices.push_back(v);
    }
    builder.indices = CUBE_INDICES;

    SubMesh sm;
    sm.firstIndex   = 0;
    sm.indexCount   = static_cast<uint32_t>(builder.indices.size());
    sm.materialId   = -1;
    sm.hasTexture   = false;
    sm.diffuseColor = glm::vec3(1.0f);
    builder.subMeshes.push_back(sm);

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const auto& v : builder.vertices) {
        mn = glm::min(mn, v.position);
        mx = glm::max(mx, v.position);
    }
    builder.bboxMin = mn;
    builder.bboxMax = mx;
    builder.boundingRadius = glm::length(mx - mn) * 0.5f;

    return std::make_shared<Model>(device, builder, builder.boundingRadius);
}

// ------------------------------------------------------------------
// Builder::loadModel — multi-material aware + defensive bounds checks
// ------------------------------------------------------------------

void Model::Builder::loadModel(const std::string& filepath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> tinyMaterials;
    std::string warn, err;

    std::string baseDir;
    size_t lastSlash = filepath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        baseDir = filepath.substr(0, lastSlash + 1);
    }

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &tinyMaterials, &warn, &err,
                               filepath.c_str(), baseDir.c_str());
    if (!warn.empty()) std::cout << "[OBJ WARN] " << warn << std::endl;
    if (!err.empty())  std::cout << "[OBJ ERR ] " << err  << std::endl;
    if (!ok) {
        throw std::runtime_error("tinyobj::LoadObj failed for " + filepath);
    }

    std::cout << "[OBJ] " << filepath
              << ": shapes="    << shapes.size()
              << ", positions=" << (attrib.vertices.size()  / 3)
              << ", normals="   << (attrib.normals.size()   / 3)
              << ", texcoords=" << (attrib.texcoords.size() / 2)
              << ", materials=" << tinyMaterials.size() << std::endl;

    materials.clear();
    materials.reserve(tinyMaterials.size());
    for (auto& tm : tinyMaterials) {
        MaterialInfo mi;
        mi.name          = tm.name;
        mi.diffuseColor  = { tm.diffuse[0],  tm.diffuse[1],  tm.diffuse[2]  };
        mi.specularColor = { tm.specular[0], tm.specular[1], tm.specular[2] };
        mi.shininess     = tm.shininess;
        mi.alpha = tm.dissolve;
        mi.illum = tm.illum;

        if (!tm.diffuse_texname.empty()) {
            if (tm.diffuse_texname[0] == '/' || tm.diffuse_texname[0] == '\\') {
                mi.diffuseTexturePath = tm.diffuse_texname;
            } else {
                mi.diffuseTexturePath = baseDir + tm.diffuse_texname;
            }
        }

        std::string normName = !tm.normal_texname.empty()
                       ? tm.normal_texname
                       : tm.bump_texname;
        if (!normName.empty()) {
            if (normName[0] == '/' || normName[0] == '\\') {
                mi.normalTexturePath = normName;
            } else {
                mi.normalTexturePath = baseDir + normName;
            }
            std::cout << "[MTL] '" << mi.name << "' norm=" << mi.normalTexturePath << std::endl;
        }

        std::cout << "[MTL] '" << mi.name
                  << "' Kd=(" << mi.diffuseColor.r << ","
                              << mi.diffuseColor.g << ","
                              << mi.diffuseColor.b << ")";
        if (!mi.diffuseTexturePath.empty()) {
            std::cout << " map_Kd=" << mi.diffuseTexturePath;
        }
        std::cout << std::endl;

        materials.push_back(std::move(mi));
    }

    diffuseTexturePath.clear();
    for (auto& m : materials) {
        if (!m.diffuseTexturePath.empty()) {
            diffuseTexturePath = m.diffuseTexturePath;
            break;
        }
    }

    vertices.clear();
    indices.clear();
    subMeshes.clear();

    std::unordered_map<Vertex, uint32_t> uniqueVertices;
    std::unordered_map<int, std::vector<uint32_t>> perMatIndices;

    const size_t nPos = attrib.vertices.size();
    const size_t nNor = attrib.normals.size();
    const size_t nUv  = attrib.texcoords.size();
    const size_t nCol = attrib.colors.size();
    const bool haveNormals = (nNor >= 3);

    auto readPos = [&](int vi, glm::vec3& out) -> bool {
        if (vi < 0) return false;
        size_t base = static_cast<size_t>(3) * static_cast<size_t>(vi);
        if (base + 2 >= nPos) return false;
        out = { attrib.vertices[base + 0],
                attrib.vertices[base + 1],
                attrib.vertices[base + 2] };
        return true;
    };

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        const size_t numFaces = shape.mesh.num_face_vertices.size();

        for (size_t f = 0; f < numFaces; ++f) {
            size_t fv = shape.mesh.num_face_vertices[f];
            if (fv < 3) { indexOffset += fv; continue; }

            int matId = -1;
            if (f < shape.mesh.material_ids.size()) {
                matId = shape.mesh.material_ids[f];
            }
            const bool validMat  = (matId >= 0 &&
                                    matId < static_cast<int>(materials.size()));
            const bool matHasTex = validMat &&
                                   !materials[matId].diffuseTexturePath.empty();

            // Flat face normal — used when OBJ has no normals (Wings 3D, some
            // Blender exports, many CAD tools).
            glm::vec3 faceFlatNormal(0.0f, 1.0f, 0.0f);
            if (!haveNormals && indexOffset + 2 < shape.mesh.indices.size()) {
                glm::vec3 p0, p1, p2;
                if (readPos(shape.mesh.indices[indexOffset + 0].vertex_index, p0) &&
                    readPos(shape.mesh.indices[indexOffset + 1].vertex_index, p1) &&
                    readPos(shape.mesh.indices[indexOffset + 2].vertex_index, p2)) {
                    glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
                    float len = glm::length(n);
                    if (len > 1e-8f) faceFlatNormal = n / len;
                }
            }

            for (size_t v = 0; v < fv; ++v) {
                const size_t globalFaceIdx = indexOffset + v;
                if (globalFaceIdx >= shape.mesh.indices.size()) break;

                tinyobj::index_t idx = shape.mesh.indices[globalFaceIdx];
                Vertex vertex{};

                if (!readPos(idx.vertex_index, vertex.position)) continue;

                if (matHasTex) {
                    vertex.color = glm::vec3(1.0f);
                } else if (validMat) {
                    vertex.color = materials[matId].diffuseColor;
                } else if (idx.vertex_index >= 0) {
                    size_t cbase = static_cast<size_t>(3) *
                                   static_cast<size_t>(idx.vertex_index);
                    if (cbase + 2 < nCol) {
                        vertex.color = { attrib.colors[cbase + 0],
                                         attrib.colors[cbase + 1],
                                         attrib.colors[cbase + 2] };
                    } else {
                        vertex.color = glm::vec3(1.0f);
                    }
                } else {
                    vertex.color = glm::vec3(1.0f);
                }

                if (validMat) {
                    vertex.specularColor = materials[matId].specularColor;
                    vertex.shininess     = materials[matId].shininess;
                }

                bool normalSet = false;
                if (haveNormals && idx.normal_index >= 0) {
                    size_t nbase = static_cast<size_t>(3) *
                                   static_cast<size_t>(idx.normal_index);
                    if (nbase + 2 < nNor) {
                        vertex.normal = { attrib.normals[nbase + 0],
                                          attrib.normals[nbase + 1],
                                          attrib.normals[nbase + 2] };
                        normalSet = true;
                    }
                }
                if (!normalSet) vertex.normal = faceFlatNormal;

                if (idx.texcoord_index >= 0) {
                    size_t tbase = static_cast<size_t>(2) *
                                   static_cast<size_t>(idx.texcoord_index);
                    if (tbase + 1 < nUv) {
                        vertex.uv = { attrib.texcoords[tbase + 0],
                                      attrib.texcoords[tbase + 1] };
                    }
                }

                uint32_t globalIdx;
                auto it = uniqueVertices.find(vertex);
                if (it == uniqueVertices.end()) {
                    globalIdx = static_cast<uint32_t>(vertices.size());
                    uniqueVertices[vertex] = globalIdx;
                    vertices.push_back(vertex);
                } else {
                    globalIdx = it->second;
                }
                perMatIndices[matId].push_back(globalIdx);
            }
            indexOffset += fv;
        }
    }

    indices.clear();
    subMeshes.clear();
    subMeshes.reserve(perMatIndices.size());

    for (auto& kv : perMatIndices) {
        const int matId   = kv.first;
        auto&     localIx = kv.second;
        if (localIx.empty()) continue;

        SubMesh sm;
        sm.firstIndex = static_cast<uint32_t>(indices.size());
        sm.indexCount = static_cast<uint32_t>(localIx.size());
        sm.materialId = matId;

        if (matId >= 0 && matId < static_cast<int>(materials.size())) {
            sm.diffuseColor   = materials[matId].diffuseColor;
            sm.hasTexture     = !materials[matId].diffuseTexturePath.empty();
            sm.alpha          = materials[matId].alpha;
            sm.isTransparent  = (materials[matId].alpha < 0.999f);
        } else {
            sm.diffuseColor   = glm::vec3(1.0f);
            sm.hasTexture     = false;
            sm.alpha          = 1.0f;
            sm.isTransparent  = false;
        }

        indices.insert(indices.end(), localIx.begin(), localIx.end());
        subMeshes.push_back(sm);
    }

    if (subMeshes.empty() && !indices.empty()) {
        SubMesh sm;
        sm.firstIndex   = 0;
        sm.indexCount   = static_cast<uint32_t>(indices.size());
        sm.materialId   = -1;
        sm.diffuseColor = glm::vec3(1.0f);
        sm.hasTexture   = false;
        subMeshes.push_back(sm);
    }

    computeTangents();
    if (vertices.empty()) {
        bboxMin = glm::vec3(0.0f);
        bboxMax = glm::vec3(0.0f);
        boundingRadius = 0.0f;
        std::cout << "[MODEL] WARNING: " << filepath
                  << " produced 0 vertices — model will be empty\n";
    } else {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        for (const auto& v : vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        bboxMin = mn;
        bboxMax = mx;
        boundingRadius = glm::length(mx - mn) * 0.5f;
    }

    std::cout << "[MODEL] " << filepath
              << ": verts="     << vertices.size()
              << ", tris="      << indices.size() / 3
              << ", subMeshes=" << subMeshes.size()
              << ", materials=" << materials.size()
              << ", normals="   << (haveNormals ? "from OBJ" : "computed flat")
              << std::endl;
}

void Model::Builder::computeTangents() {
    if (vertices.empty() || indices.empty()) return;

    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];

        glm::vec3 e1 = v1.position - v0.position;
        glm::vec3 e2 = v2.position - v0.position;
        glm::vec2 du1 = v1.uv - v0.uv;
        glm::vec2 du2 = v2.uv - v0.uv;

        float denom = du1.x * du2.y - du2.x * du1.y;
        if (std::abs(denom) < 1e-8f) continue;
        float f = 1.0f / denom;

        glm::vec3 t = f * (du2.y * e1 - du1.y * e2);

        tangents[i0] += t;
        tangents[i1] += t;
        tangents[i2] += t;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        glm::vec3 n = vertices[i].normal;
        glm::vec3 t = tangents[i];

        t = t - n * glm::dot(n, t);
        float len = glm::length(t);

        if (len > 1e-8f) {
            vertices[i].tangent = t / len;
        } else {
            glm::vec3 up = std::abs(n.y) < 0.999f
                           ? glm::vec3(0, 1, 0)
                           : glm::vec3(1, 0, 0);
            vertices[i].tangent = glm::normalize(glm::cross(n, up));
        }
    }
}
// ------------------------------------------------------------------
// GPU resources
// ------------------------------------------------------------------

void Model::createVertexBuffers(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        throw std::runtime_error(
            "Model::createVertexBuffers: no vertices — check OBJ file");
    }
    vertexCount = static_cast<uint32_t>(vertices.size());
    assert(vertexCount >= 3 && "Vertex count must be at least 3");
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
    uint32_t vertexSize = sizeof(vertices[0]);

    Buffer stagingBuffer{
        device, vertexSize, vertexCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    stagingBuffer.map();
    stagingBuffer.writeToBuffer((void*)vertices.data());

    vertexBuffer = std::make_unique<Buffer>(
        device, vertexSize, vertexCount,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    device.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
}

void Model::createIndexBuffers(const std::vector<uint32_t>& indices) {
    indexCount = static_cast<uint32_t>(indices.size());
    hasIndexBuffer = indexCount > 0;
    if (!hasIndexBuffer) return;

    VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
    uint32_t indexSize = sizeof(indices[0]);

    Buffer stagingBuffer{
        device, indexSize, indexCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    stagingBuffer.map();
    stagingBuffer.writeToBuffer((void*)indices.data());

    indexBuffer = std::make_unique<Buffer>(
        device, indexSize, indexCount,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    device.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
}

void Model::createSubMeshTextures(const Builder& /*builder*/) {
    std::unordered_map<std::string, std::shared_ptr<Texture>> diffuseCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> normalCache;

    for (auto& sm : subMeshes_) {
        if (sm.materialId < 0 ||
            sm.materialId >= static_cast<int>(materials_.size())) {
            sm.hasTexture = false;
            sm.hasNormalMap = false;
            continue;
        }

        const auto& mat = materials_[sm.materialId];

        // ---- diffuse ----
        if (!mat.diffuseTexturePath.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(mat.diffuseTexturePath, ec)) {
                std::cout << "[MTL] diffuse not found: " << mat.diffuseTexturePath << "\n";
                sm.hasTexture = false;
            } else {
                auto it = diffuseCache.find(mat.diffuseTexturePath);
                if (it != diffuseCache.end()) {
                    sm.texture = it->second;
                    sm.hasTexture = true;
                } else {
                    try {
                        sm.texture = std::make_shared<Texture>(
                            device, mat.diffuseTexturePath, /*isSRGB=*/true);
                        diffuseCache[mat.diffuseTexturePath] = sm.texture;
                        sm.hasTexture = true;
                        std::cout << "[MTL] loaded diffuse: " << mat.diffuseTexturePath << "\n";
                    } catch (const std::exception& e) {
                        std::cout << "[MTL] failed diffuse " << mat.diffuseTexturePath
                                  << ": " << e.what() << "\n";
                        sm.hasTexture = false;
                    }
                }
            }
        } else {
            sm.hasTexture = false;
        }

        // ---- normal ----
        if (!mat.normalTexturePath.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(mat.normalTexturePath, ec)) {
                std::cout << "[MTL] normal not found: " << mat.normalTexturePath << "\n";
                sm.hasNormalMap = false;
            } else {
                auto it = normalCache.find(mat.normalTexturePath);
                if (it != normalCache.end()) {
                    sm.normalTexture = it->second;
                    sm.hasNormalMap = true;
                } else {
                    try {
                        // Normal map = LINEAR (UNORM), не sRGB!
                        sm.normalTexture = std::make_shared<Texture>(
                            device, mat.normalTexturePath, /*isSRGB=*/false);
                        normalCache[mat.normalTexturePath] = sm.normalTexture;
                        sm.hasNormalMap = true;
                        std::cout << "[MTL] loaded normal: " << mat.normalTexturePath << "\n";
                    } catch (const std::exception& e) {
                        std::cout << "[MTL] failed normal " << mat.normalTexturePath
                                  << ": " << e.what() << "\n";
                        sm.hasNormalMap = false;
                    }
                }
            }
        }
    }
}
// ------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------

void Model::bind(VkCommandBuffer commandBuffer) {
    VkBuffer     buffers[] = { vertexBuffer->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    if (hasIndexBuffer) {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0,
                             VK_INDEX_TYPE_UINT32);
    }
}

void Model::draw(VkCommandBuffer commandBuffer) {
    if (subMeshes_.empty()) {
        if (hasIndexBuffer) {
            vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
        } else {
            vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        }
        return;
    }
    for (size_t i = 0; i < subMeshes_.size(); ++i) {
        drawSubMesh(commandBuffer, i);
    }
}

void Model::drawSubMesh(VkCommandBuffer commandBuffer, size_t subMeshIndex) {
    if (subMeshIndex >= subMeshes_.size()) return;
    const auto& sm = subMeshes_[subMeshIndex];
    if (hasIndexBuffer) {
        vkCmdDrawIndexed(commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
    } else {
        vkCmdDraw(commandBuffer, sm.indexCount, 1, sm.firstIndex, 0);
    }
}

// ------------------------------------------------------------------
// Vertex input layout
// ------------------------------------------------------------------

std::vector<VkVertexInputBindingDescription> Model::Vertex::getBindingDescriptions() {
    std::vector<VkVertexInputBindingDescription> b(1);
    b[0].binding   = 0;
    b[0].stride    = sizeof(Vertex);
    b[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return b;
}

std::vector<VkVertexInputAttributeDescription> Model::Vertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> a;
    a.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
    a.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)    });
    a.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)   });
    a.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)       });
    a.push_back({ 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent)  });
    return a;
}

// ------------------------------------------------------------------
// Ray tracing (unchanged)
// ------------------------------------------------------------------

void Model::buildBlas(Device& device) {
    if (blas != VK_NULL_HANDLE) return;

    VkBuffer vertexBufferHandle = vertexBuffer->getBuffer();
    VkBuffer indexBufferHandle  = indexBuffer->getBuffer();

    VkDeviceAddress vertexAddress = device.getBufferDeviceAddress(vertexBufferHandle);
    VkDeviceAddress indexAddress  = device.getBufferDeviceAddress(indexBufferHandle);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress;
    geometry.geometry.triangles.vertexStride = sizeof(Vertex);
    geometry.geometry.triangles.maxVertex    = vertexCount;
    geometry.geometry.triangles.indexType    = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.indexData.deviceAddress = indexAddress;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geometry;

    uint32_t primitiveCount = indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    Device::vkGetAccelerationStructureBuildSizesKHR(
        device.device(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &primitiveCount, &sizeInfo);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = sizeInfo.accelerationStructureSize;
    bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    device.createBuffer(bufferInfo.size, bufferInfo.usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        blasBuffer, blasMemory);

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = blasBuffer;
    createInfo.size   = sizeInfo.accelerationStructureSize;
    createInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (Device::vkCreateAccelerationStructureKHR(
            device.device(), &createInfo, nullptr, &blas) != VK_SUCCESS) {
        throw std::runtime_error("failed to create BLAS");
    }

    VkBuffer       scratchBuffer;
    VkDeviceMemory scratchMemory;
    VkBufferCreateInfo scratchInfo{};
    scratchInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    scratchInfo.size  = sizeInfo.buildScratchSize;
    scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    device.createBuffer(scratchInfo.size, scratchInfo.usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        scratchBuffer, scratchMemory);

    VkDeviceAddress scratchAddress = device.getBufferDeviceAddress(scratchBuffer);
    buildInfo.dstAccelerationStructure  = blas;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount  = primitiveCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex     = 0;
    rangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    Device::vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
    device.endSingleTimeCommands(cmd);

    vkDestroyBuffer(device.device(), scratchBuffer, nullptr);
    vkFreeMemory(device.device(),    scratchMemory, nullptr);
}

void Model::destroyBlas(Device& device) {
    if (blas != VK_NULL_HANDLE) {
        Device::vkDestroyAccelerationStructureKHR(device.device(), blas, nullptr);
        blas = VK_NULL_HANDLE;
    }
    if (blasBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device.device(), blasBuffer, nullptr);
        blasBuffer = VK_NULL_HANDLE;
    }
    if (blasMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device.device(), blasMemory, nullptr);
        blasMemory = VK_NULL_HANDLE;
    }
}

} // namespace enginev