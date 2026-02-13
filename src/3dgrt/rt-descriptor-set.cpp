#include "rt-descriptor-set.h"

#include "vulkan/vkcontext.h"

#include <iostream>
#include <vector>


namespace vk3dgrt {

bool RayTraceDescriptorSet::initialize(VkContext* context)
{
    ctx_ = context;

    std::cout << "[RayTraceDescriptorSet] Initializing..." << std::endl;

    if (!createLayout())
    {
        std::cerr << "[RayTraceDescriptorSet] Failed to create layout" << std::endl;
        return false;
    }

    if (!createPool())
    {
        std::cerr << "[RayTraceDescriptorSet] Failed to create pool" << std::endl;
        return false;
    }

    if (!allocateSet())
    {
        std::cerr << "[RayTraceDescriptorSet] Failed to allocate set" << std::endl;
        return false;
    }

    return true;
}


void RayTraceDescriptorSet::cleanup(VkDevice device)
{
    allocator_.cleanup(device);
    layout_.cleanup(device);

    descriptorSet_    = VK_NULL_HANDLE;
    ctx_              = nullptr;
    dirty_            = true;
    cachedTlasHandle_ = VK_NULL_HANDLE;
    boundTlas_        = VK_NULL_HANDLE;
    boundOutputView_  = VK_NULL_HANDLE;
    boundSceneBounds_ = VK_NULL_HANDLE;
}


bool RayTraceDescriptorSet::update(VkAccelerationStructureKHR tlas,
                                   VkImageView outputImageView,
                                   VkBuffer sceneBoundsBuffer,
                                   VkDeviceSize sceneBoundsSize)
{
    // Detect resource changes
    bool resourcesChanged = (boundTlas_        != tlas)             ||
                            (boundOutputView_  != outputImageView)  ||
                            (boundSceneBounds_ != sceneBoundsBuffer);

    if (resourcesChanged)
    {
        dirty_ = true;
    }

    if (!dirty_)
    {
        return true;
    }

    VkDevice device = ctx_->getDevice();

    // Cache TLAS handle (must stay alive during vkUpdateDescriptorSets)
    cachedTlasHandle_ = tlas;

    // Update tracking
    boundTlas_        = tlas;
    boundOutputView_  = outputImageView;
    boundSceneBounds_ = sceneBoundsBuffer;

    // Prepare writes
    std::vector<VkWriteDescriptorSet> writes;

    // Binding 0: TLAS
    VkWriteDescriptorSetAccelerationStructureKHR tlasWrite{
        .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &cachedTlasHandle_
    };

    VkWriteDescriptorSet tlasDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext           = &tlasWrite,
        .dstSet          = descriptorSet_,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
    };
    writes.push_back(tlasDescriptor);

    // Binding 1: Output Image
    VkDescriptorImageInfo outputImageInfo{
        .sampler     = VK_NULL_HANDLE,
        .imageView   = outputImageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    VkWriteDescriptorSet outputImageDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo      = &outputImageInfo
    };
    writes.push_back(outputImageDescriptor);

    // Binding 2: Scene Bounds UBO
    VkDescriptorBufferInfo sceneBoundsBufferInfo{
        .buffer = sceneBoundsBuffer,
        .offset = 0,
        .range  = sceneBoundsSize
    };

    VkWriteDescriptorSet sceneBoundsDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &sceneBoundsBufferInfo
    };
    writes.push_back(sceneBoundsDescriptor);

    // Update
    vkUpdateDescriptorSets(
        device,
        static_cast<uint32_t>(writes.size()),
        writes.data(),
        0,
        nullptr
    );

    dirty_ = false;

    return true;
}


void RayTraceDescriptorSet::updateOutputImage(VkImageView newImageView)
{
    VkDescriptorImageInfo outputImageInfo{
        .sampler     = VK_NULL_HANDLE,
        .imageView   = newImageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    VkWriteDescriptorSet outputImageDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo      = &outputImageInfo
    };

    vkUpdateDescriptorSets(ctx_->getDevice(), 1, &outputImageDescriptor, 0, nullptr);

    boundOutputView_ = newImageView;
}


void RayTraceDescriptorSet::bind(VkCommandBuffer cmd, VkPipelineLayout layout) const
{
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        layout,
        0,
        1,
        &descriptorSet_,
        0,
        nullptr
    );
}


bool RayTraceDescriptorSet::createLayout()
{
    std::vector<DescriptorSetLayoutBinding> bindings = {
        // Binding 0: TLAS
        {
            .binding    = 0,
            .type       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 1: Output Image
        {
            .binding    = 1,
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 2: Scene Bounds UBO (Gaussian/SH buffers moved to BDA push constants)
        {
            .binding    = 2,
            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        }
    };

    layout_.create(ctx_->getDevice(), bindings);

    return layout_.layout != VK_NULL_HANDLE;
}


bool RayTraceDescriptorSet::createPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             1 }
    };

    allocator_.create(ctx_->getDevice(), 1, poolSizes);

    return allocator_.pool != VK_NULL_HANDLE;
}


bool RayTraceDescriptorSet::allocateSet()
{
    descriptorSet_ = allocator_.allocate(
        ctx_->getDevice(),
        layout_.layout
    );

    if (descriptorSet_ == VK_NULL_HANDLE)
    {
        return false;
    }

    dirty_ = true;
    return true;
}

}   // namespace vk3dgrt