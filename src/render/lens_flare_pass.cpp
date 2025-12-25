#include "lens_flare_pass.hpp"

#include "device.hpp"
#include "descriptors.hpp"   
#include <stdexcept>
#include <algorithm>

namespace enginev {

    LensFlarePass::LensFlarePass(Device& device, const std::string& compSpvPath)
        : device_{ device }, compSpvPath_{ compSpvPath } {
        createDescriptorSetLayout_();
    }

    LensFlarePass::~LensFlarePass() {
        destroy();
        destroyDescriptorSetLayout_();
    }

    void LensFlarePass::destroy() {
        destroyPipeline_();
        destroyImage_();
    }

    void LensFlarePass::recreate(VkExtent2D swapExtent, float scale) {
        destroyPipeline_();
        destroyImage_();

        flareExtent_.width = std::max(1u, static_cast<uint32_t>(swapExtent.width * scale));
        flareExtent_.height = std::max(1u, static_cast<uint32_t>(swapExtent.height * scale));

        createFlareImage_();
        createFlareView_();
        createFlareSampler_();

        createPipelineLayout_();
        createPipeline_();
    }

    VkDescriptorSetLayout LensFlarePass::getDescriptorSetLayout() const {
        if (!lensSetLayout_) return VK_NULL_HANDLE;
        return lensSetLayout_->getDescriptorSetLayout();
    }

    void LensFlarePass::createDescriptorSetLayout_() {
        lensSetLayout_ =
            DescriptorSetLayout::Builder(device_)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
    }

    void LensFlarePass::destroyDescriptorSetLayout_() {
        lensSetLayout_.reset();
    }

    void LensFlarePass::createFlareImage_() {

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = flareExtent_.width;
        printf("\n\n\nflareExtent %u x %u", flareExtent_.width, flareExtent_.height);
        imageInfo.extent.height = flareExtent_.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = flareFormat_;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage =
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        device_.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            flareImage_,
            flareImageMemory_);

        device_.transitionImageLayout(
            flareImage_,
            flareFormat_,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            1);
    }

    std::vector<char> LensFlarePass::readFile(const std::string& filepath) {
        std::ifstream file{ filepath, std::ios::ate | std::ios::binary };

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filepath);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();
        return buffer;
    }

    VkShaderModule LensFlarePass::createShaderModuleLocal(const std::vector<char>& code) {
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

    void LensFlarePass::createFlareView_() {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = flareImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = flareFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_.device(), &viewInfo, nullptr, &flareImageView_) != VK_SUCCESS) {
            throw std::runtime_error("LensFlarePass: failed to create flare image view");
        }
    }

    void LensFlarePass::createFlareSampler_() {
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

        if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &flareSampler_) != VK_SUCCESS) {
            throw std::runtime_error("LensFlarePass: failed to create flare sampler");
        }
    }

    void LensFlarePass::createPipelineLayout_() {
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }

        VkDescriptorSetLayout setLayouts[] = {
            lensSetLayout_->getDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = setLayouts;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("LensFlarePass: failed to create pipeline layout");
        }
    }

    void LensFlarePass::createPipeline_() {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_.device(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }

        auto compCode = readFile("../shaders/lens_flare.comp.spv");
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
            throw std::runtime_error("LensFlarePass: failed to create compute pipeline");
        }

        vkDestroyShaderModule(device_.device(), compModule, nullptr);
    }

    void LensFlarePass::destroyImage_() {
        if (flareSampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_.device(), flareSampler_, nullptr);
            flareSampler_ = VK_NULL_HANDLE;
        }
        if (flareImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_.device(), flareImageView_, nullptr);
            flareImageView_ = VK_NULL_HANDLE;
        }
        if (flareImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_.device(), flareImage_, nullptr);
            flareImage_ = VK_NULL_HANDLE;
        }
        if (flareImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_.device(), flareImageMemory_, nullptr);
            flareImageMemory_ = VK_NULL_HANDLE;
        }
    }

    void LensFlarePass::destroyPipeline_() {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_.device(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    void LensFlarePass::transitionToGeneral(VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = flareImage_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    }

    void LensFlarePass::transitionToShaderRead(VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = flareImage_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    }

    void LensFlarePass::dispatch(VkCommandBuffer cmd, VkDescriptorSet lensSet) {
        if (pipeline_ == VK_NULL_HANDLE) {
            throw std::runtime_error("LensFlarePass: pipeline is not created");
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout_,
            0, 1, &lensSet,
            0, nullptr);

        uint32_t gx = (flareExtent_.width + 7) / 8;
        uint32_t gy = (flareExtent_.height + 7) / 8;

        vkCmdDispatch(cmd, gx, gy, 1);
    }

} // namespace enginev