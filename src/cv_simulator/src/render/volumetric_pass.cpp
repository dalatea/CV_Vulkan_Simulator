#include "volumetric_pass.hpp"
#include <stdexcept>
#include <fstream>
#include <algorithm>

namespace enginev {

VolumetricPass::VolumetricPass(Device& device, const std::string& compSpvPath)
    : device_{ device } {
        compSpvPath_ = getShaderPath("volumetric_rays.comp.spv");
        createDescriptorSetLayout_();
    }
VolumetricPass::~VolumetricPass() { destroy(); }

void VolumetricPass::destroy() {
    destroyPipeline_();
    destroyImage_();
}
void VolumetricPass::recreate(VkExtent2D swapExtent, float scale) {
    destroyPipeline_();
    destroyImage_();

    godRaysExtent_.width = std::max(1u, static_cast<uint32_t>(swapExtent.width * scale));
    godRaysExtent_.height = std::max(1u, static_cast<uint32_t>(swapExtent.height * scale));

    createGodRaysImage_();
    createGodRaysView_();
    createGodRaysSampler_();
    createPipelineLayout_();  
    createPipeline_();
}

VkDescriptorSetLayout VolumetricPass::getDescriptorSetLayout() const {
    if (!volSetLayout_) return VK_NULL_HANDLE;
    return volSetLayout_->getDescriptorSetLayout();
}


void VolumetricPass::createDescriptorSetLayout_() {
    volSetLayout_ =
        DescriptorSetLayout::Builder(device_)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // GlobalUbo!
        .build();
}


std::string VolumetricPass::getShaderPath(const std::string& filename) {
        const char* ament_path = std::getenv("AMENT_PREFIX_PATH");
        std::stringstream ss(ament_path);
        std::string path_item;
        
        while (std::getline(ss, path_item, ':')) {
            std::string full_path = path_item + "/share/cv_simulator/shaders/" + filename;
            if (std::filesystem::exists(full_path)) {
                std::cout << "[DEBUG] Found shader at: " << full_path << std::endl;
                return full_path;
            }
        }
    }

void VolumetricPass::destroyDescriptorSetLayout_() {
    volSetLayout_.reset();
}

void VolumetricPass::createGodRaysImage_() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = godRaysExtent_.width;
    imageInfo.extent.height = godRaysExtent_.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = godRaysFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    device_.createImageWithInfo(
        imageInfo,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        godRaysImage_,
        godRaysImageMemory_);

    device_.transitionImageLayout(
        godRaysImage_,
        godRaysFormat_,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        1);
}

void VolumetricPass::createGodRaysView_() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = godRaysImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = godRaysFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_.device(), &viewInfo, nullptr, &godRaysImageView_) != VK_SUCCESS) {
        throw std::runtime_error("VolumetricPass: failed to create image view");
    }
}

void VolumetricPass::createGodRaysSampler_() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &godRaysSampler_) != VK_SUCCESS) {
        throw std::runtime_error("VolumetricPass: failed to create sampler");
    }
}

void VolumetricPass::createPipelineLayout_() {
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    VkDescriptorSetLayout setLayouts[] = { volSetLayout_->getDescriptorSetLayout() };
    
    // ← Push constants для параметров
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 8;  // vec2 + 4 float + 2 pad = 32 bytes

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("VolumetricPass: failed to create pipeline layout");
    }
}


void VolumetricPass::createPipeline_() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_.device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    auto compCode = readFile(compSpvPath_);
    VkShaderModule compModule = createShaderModuleLocal(compCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.stage = stageInfo;
    pipeInfo.layout = pipelineLayout_;

    if (vkCreateComputePipelines(device_.device(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_.device(), compModule, nullptr);
        throw std::runtime_error("VolumetricPass: failed to create pipeline");
    }

    vkDestroyShaderModule(device_.device(), compModule, nullptr);
}

void VolumetricPass::destroyImage_() {
    if (godRaysSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_.device(), godRaysSampler_, nullptr);
        godRaysSampler_ = VK_NULL_HANDLE;
    }
    if (godRaysImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_.device(), godRaysImageView_, nullptr);
        godRaysImageView_ = VK_NULL_HANDLE;
    }
    if (godRaysImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_.device(), godRaysImage_, nullptr);
        godRaysImage_ = VK_NULL_HANDLE;
    }
    if (godRaysImageMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_.device(), godRaysImageMemory_, nullptr);
        godRaysImageMemory_ = VK_NULL_HANDLE;
    }
}

void VolumetricPass::destroyPipeline_() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_.device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
}

std::vector<char> VolumetricPass::readFile(const std::string& filepath) {
    std::ifstream file{ filepath, std::ios::ate | std::ios::binary };
    if (!file.is_open()) throw std::runtime_error("failed to open: " + filepath);
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule VolumetricPass::createShaderModuleLocal(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_.device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }
    return shaderModule;
}

void VolumetricPass::transitionToGeneral(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = firstFrame_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    firstFrame_ = false;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = godRaysImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VolumetricPass::transitionToShaderRead(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = godRaysImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void VolumetricPass::dispatch(VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                               const void* pushData, uint32_t pushDataSize) {
    if (pipeline_ == VK_NULL_HANDLE) throw std::runtime_error("pipeline not created");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_, 0, 1, &descriptorSet, 0, nullptr);

    if (pushData && pushDataSize > 0) {
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, pushDataSize, pushData);
    }

    uint32_t gx = (godRaysExtent_.width  + 7) / 8;
    uint32_t gy = (godRaysExtent_.height + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);
}

} // namespace enginev