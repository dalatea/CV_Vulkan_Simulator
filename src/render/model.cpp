#include "model.hpp"
#include "utils.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <cassert>
#include <cstring>
#include <unordered_map>


namespace std {


	template <>


	struct hash<enginev::Model::Vertex> {
		size_t operator()(enginev::Model::Vertex const& vertex) const {
			size_t seed = 0;
			enginev::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
			return seed;
		}
	};
}

namespace enginev {
	Model::Model(Device& device, const Model::Builder &builder, float radius) 
		: device{device}, boundingRadius(radius) {
		createVertexBuffers(builder.vertices);
		createIndexBuffers(builder.indices);
	}

	Model::~Model() {}

	std::unique_ptr<Model> Model::createModelFromFile(
		Device& device, const std::string& filepath) {
		Builder builder{};
		builder.loadModel(filepath);

		return std::make_unique<Model>(device, builder, builder.boundingRadius);
	}

	std::shared_ptr<Model> Model::createSkyboxCube(Device& device) {
		static const std::vector<glm::vec3> CUBE_POSITIONS = {
        {-1, -1, -1}, {1, -1, -1}, {1,  1, -1}, {-1,  1, -1},  // back
        {-1, -1,  1}, {1, -1,  1}, {1,  1,  1}, {-1,  1,  1}   // front
    	};

		static const std::vector<uint32_t> CUBE_INDICES = {
			0,1,2, 2,3,0,  // back
			4,5,6, 6,7,4,  // front
			0,4,7, 7,3,0,  // left
			1,5,6, 6,2,1,  // right
			3,2,6, 6,7,3,  // top
			0,1,5, 5,4,0   // bottom
		};

		Builder builder{};

		builder.vertices.reserve(CUBE_POSITIONS.size());
		for (auto &p : CUBE_POSITIONS) {
			Vertex v{};
			v.position = p;
			v.color = {1, 1, 1};
			v.normal = {0, 0, 0};
			v.uv = {0, 0};
			builder.vertices.push_back(v);
		}

		builder.indices = CUBE_INDICES;

		glm::vec3 min(std::numeric_limits<float>::max());
		glm::vec3 max(std::numeric_limits<float>::lowest());
		for (const auto& v : builder.vertices) {
			min = glm::min(min, v.position);
			max = glm::max(max, v.position);
		}
		builder.bboxMin = min;
		builder.bboxMax = max;
		glm::vec3 extent = max - min;
		builder.boundingRadius = glm::length(extent) * 0.5f;

		return std::make_shared<Model>(device, builder, builder.boundingRadius);
	}

	void Model::createVertexBuffers(const std::vector<Vertex>& vertices) {
		vertexCount = static_cast<uint32_t>(vertices.size());
		assert(vertexCount >= 3 && "Vertex count must be at least 3");
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
		uint32_t vertexSize = sizeof(vertices[0]);


		Buffer stagingBuffer{
			device,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());

		vertexBuffer = std::make_unique<Buffer>(
			device,
			vertexSize,
			vertexCount,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		device.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
	}

	void Model::Builder::loadModel(const std::string& filepath) {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
			throw std::runtime_error(warn + err);
		}

		vertices.clear();
		indices.clear();

		std::unordered_map<Vertex, uint32_t> uniqueVertices{};
		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex{};

				if (index.vertex_index >= 0) {
					vertex.position = {
						attrib.vertices[3 * index.vertex_index + 0],
						attrib.vertices[3 * index.vertex_index + 1],
						attrib.vertices[3 * index.vertex_index + 2],
					};

					vertex.color = {
						attrib.colors[3 * index.vertex_index + 0],
						attrib.colors[3 * index.vertex_index + 1],
						attrib.colors[3 * index.vertex_index + 2],
					};
				}

				if (index.normal_index >= 0) {
					vertex.normal = {
						attrib.normals[3 * index.normal_index + 0],
						attrib.normals[3 * index.normal_index + 1],
						attrib.normals[3 * index.normal_index + 2],
					};
				}

				if (index.texcoord_index >= 0) {
					vertex.uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						attrib.texcoords[2 * index.texcoord_index + 1],
					};
				}

				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertex);
				}
				indices.push_back(uniqueVertices[vertex]);
			}
		}

		glm::vec3 min(std::numeric_limits<float>::max());
		glm::vec3 max(std::numeric_limits<float>::lowest());

		for (const auto& v : vertices) {
			min = glm::min(min, v.position);
			min = glm::max(max, v.position);
		}

		bboxMin = min;
		bboxMax = max;

		glm::vec3 extent = max - min;
		boundingRadius = glm::length(extent) * 0.5f; 
	}

	void Model::createIndexBuffers(const std::vector<uint32_t>& indices) {
		indexCount = static_cast<uint32_t>(indices.size());
		hasIndexBuffer = indexCount > 0;

		if (!hasIndexBuffer) {
			return;
		}

		VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
		uint32_t indexSize = sizeof(indices[0]);

		Buffer stagingBuffer{
			device,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());

		indexBuffer = std::make_unique<Buffer>(
			device,
			indexSize,
			indexCount,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		device.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
	}

	void Model::draw(VkCommandBuffer commandBuffer) {
		if (hasIndexBuffer) {
			vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		}
		else {
			vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
		}
	}

	void Model::bind(VkCommandBuffer commandBuffer) {
		VkBuffer buffers[] = { vertexBuffer->getBuffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

		if (hasIndexBuffer) {
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
		}
	}

	std::vector<VkVertexInputBindingDescription> Model::Vertex::getBindingDescriptions() {
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex);
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescriptions;
	}

	std::vector<VkVertexInputAttributeDescription> Model::Vertex::getAttributeDescriptions() {
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
		attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
		attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) });
		attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
		attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });

		return attributeDescriptions;
	}

    void Model::buildBlas(Device& device) {
        if (blas != VK_NULL_HANDLE) return;

        VkBuffer vertexBufferHandle = vertexBuffer->getBuffer();
        VkBuffer indexBufferHandle = indexBuffer->getBuffer();

        VkDeviceAddress vertexAddress = device.getBufferDeviceAddress(vertexBufferHandle);
        VkDeviceAddress indexAddress = device.getBufferDeviceAddress(indexBufferHandle);

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress;
        geometry.geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.geometry.triangles.maxVertex = vertexCount;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geometry.geometry.triangles.indexData.deviceAddress = indexAddress;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t primitiveCount = indexCount / 3;
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        Device::vkGetAccelerationStructureBuildSizesKHR(device.device(),
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &buildInfo, &primitiveCount, &sizeInfo);

        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeInfo.accelerationStructureSize;
        bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device.createBuffer(
			bufferInfo.size,
			bufferInfo.usage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
			blasBuffer, 
			blasMemory);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = blasBuffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        if (Device::vkCreateAccelerationStructureKHR(device.device(), &createInfo, nullptr, &blas) != VK_SUCCESS) {
            throw std::runtime_error("failed to create BLAS");
        }

        // Scratch buffer
        VkBuffer scratchBuffer;
        VkDeviceMemory scratchMemory;
        VkBufferCreateInfo scratchInfo{};
        scratchInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratchInfo.size = sizeInfo.buildScratchSize;
        scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        device.createBuffer(
			scratchInfo.size,
			scratchInfo.usage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
			scratchBuffer, 
			scratchMemory);

        VkDeviceAddress scratchAddress = device.getBufferDeviceAddress(scratchBuffer);

        buildInfo.dstAccelerationStructure = blas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primitiveCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        Device::vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        device.endSingleTimeCommands(cmd);

        vkDestroyBuffer(device.device(), scratchBuffer, nullptr);
        vkFreeMemory(device.device(), scratchMemory, nullptr);
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
}