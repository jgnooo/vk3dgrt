#include "rt-descriptor-set.h"

#include "log.h"

#include "vulkan/vkcontext.h"

#include <vector>


namespace vk3dgrt {

bool RayTraceDescriptorSet::initialize(VkContext* context)
{
    ctx_ = context;

    if (!createLayout())
    {
        Log::ERR("Render") << "Failed to create layout";
        return false;
    }

    if (!createPool())
    {
        Log::ERR("Render") << "Failed to create pool";
        return false;
    }

    if (!allocateSet())
    {
        Log::ERR("Render") << "Failed to allocate set";
        return false;
    }

    // Create dummy buffer for placeholder bindings (4,5,6) when no mesh is present
    dummyBuffer_.create(ctx_, 32, BufferUsage::STORAGE, false);

    return true;
}


void RayTraceDescriptorSet::cleanup(VkDevice device)
{
    if (ctx_)
    {
        dummyBuffer_.cleanup(ctx_->getAllocator());
    }

    allocator_.cleanup(device);
    layout_.cleanup(device);

    descriptorSet_        = VK_NULL_HANDLE;
    dirty_                = true;
    cachedTlasHandle_     = VK_NULL_HANDLE;
    boundTlas_            = VK_NULL_HANDLE;
    boundOutputView_      = VK_NULL_HANDLE;
    boundSceneBounds_     = VK_NULL_HANDLE;
    cachedMeshTlasHandle_ = VK_NULL_HANDLE;
    boundMeshTlas_        = VK_NULL_HANDLE;
    boundMeshVertices_    = VK_NULL_HANDLE;
    boundMeshNormals_     = VK_NULL_HANDLE;
    boundMeshIndices_     = VK_NULL_HANDLE;
    boundMeshMaterials_   = VK_NULL_HANDLE;
    ctx_                  = nullptr;
}


bool RayTraceDescriptorSet::update(VkAccelerationStructureKHR tlas,
                                   VkImageView outputImageView,
                                   VkBuffer sceneBoundsBuffer,
                                   VkDeviceSize sceneBoundsSize,
                                   VkAccelerationStructureKHR meshTlas,
                                   VkBuffer meshVertexBuffer,
                                   VkDeviceSize meshVertexSize,
                                   VkBuffer meshNormalBuffer,
                                   VkDeviceSize meshNormalSize,
                                   VkBuffer meshIndexBuffer,
                                   VkDeviceSize meshIndexSize,
                                   VkBuffer meshMaterialBuffer,
                                   VkDeviceSize meshMaterialSize)
{
    // Detect resource changes
    bool resourcesChanged = (boundTlas_         != tlas)              ||
                            (boundOutputView_   != outputImageView)   ||
                            (boundSceneBounds_  != sceneBoundsBuffer) ||
                            (boundMeshTlas_     != meshTlas)          ||
                            (boundMeshVertices_ != meshVertexBuffer)  ||
                            (boundMeshNormals_  != meshNormalBuffer)  ||
                            (boundMeshIndices_  != meshIndexBuffer)   ||
                            (boundMeshMaterials_ != meshMaterialBuffer);

    if (resourcesChanged)
    {
        dirty_ = true;
    }

    if (!dirty_)
    {
        return true;
    }

    VkDevice device = ctx_->getDevice();

    // Cache TLAS handles (must stay alive during vkUpdateDescriptorSets)
    cachedTlasHandle_     = tlas;
    cachedMeshTlasHandle_ = meshTlas;

    // Update tracking
    boundTlas_           = tlas;
    boundOutputView_     = outputImageView;
    boundSceneBounds_    = sceneBoundsBuffer;
    boundMeshTlas_       = meshTlas;
    boundMeshVertices_   = meshVertexBuffer;
    boundMeshNormals_    = meshNormalBuffer;
    boundMeshIndices_    = meshIndexBuffer;
    boundMeshMaterials_  = meshMaterialBuffer;

    // Prepare writes
    std::vector<VkWriteDescriptorSet> writes;

    // Binding 0: Gaussian TLAS
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

    // Binding 3: Mesh TLAS (use Gaussian TLAS as placeholder when no mesh)
    // Vulkan requires all statically-used descriptors to be valid,
    // even if runtime control flow never accesses them (meshCount==0).
    cachedMeshTlasHandle_ = (meshTlas != VK_NULL_HANDLE) ? meshTlas : tlas;

    VkWriteDescriptorSetAccelerationStructureKHR meshTlasWrite{
        .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &cachedMeshTlasHandle_
    };

    VkWriteDescriptorSet meshTlasDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext           = &meshTlasWrite,
        .dstSet          = descriptorSet_,
        .dstBinding      = 3,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
    };
    writes.push_back(meshTlasDescriptor);

    // Binding 4,5,6,7: Mesh Vertex/Normal/Index/Material Buffers
    // Use dummy buffer as placeholder when no mesh buffers are present
    VkBuffer  actualVertexBuf   = (meshVertexBuffer   != VK_NULL_HANDLE) ? meshVertexBuffer   : dummyBuffer_.buffer;
    VkBuffer  actualNormalBuf   = (meshNormalBuffer    != VK_NULL_HANDLE) ? meshNormalBuffer   : dummyBuffer_.buffer;
    VkBuffer  actualIndexBuf    = (meshIndexBuffer     != VK_NULL_HANDLE) ? meshIndexBuffer    : dummyBuffer_.buffer;
    VkBuffer  actualMaterialBuf = (meshMaterialBuffer  != VK_NULL_HANDLE) ? meshMaterialBuffer : dummyBuffer_.buffer;
    VkDeviceSize actualVertexSize   = (meshVertexBuffer   != VK_NULL_HANDLE) ? meshVertexSize   : dummyBuffer_.size;
    VkDeviceSize actualNormalSize   = (meshNormalBuffer    != VK_NULL_HANDLE) ? meshNormalSize   : dummyBuffer_.size;
    VkDeviceSize actualIndexSize    = (meshIndexBuffer     != VK_NULL_HANDLE) ? meshIndexSize    : dummyBuffer_.size;
    VkDeviceSize actualMaterialSize = (meshMaterialBuffer  != VK_NULL_HANDLE) ? meshMaterialSize : dummyBuffer_.size;

    VkDescriptorBufferInfo meshVertexInfo{
        .buffer = actualVertexBuf,
        .offset = 0,
        .range  = actualVertexSize
    };
    VkWriteDescriptorSet meshVertexDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 4,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo     = &meshVertexInfo
    };
    writes.push_back(meshVertexDescriptor);

    VkDescriptorBufferInfo meshNormalInfo{
        .buffer = actualNormalBuf,
        .offset = 0,
        .range  = actualNormalSize
    };
    VkWriteDescriptorSet meshNormalDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 5,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo     = &meshNormalInfo
    };
    writes.push_back(meshNormalDescriptor);

    VkDescriptorBufferInfo meshIndexInfo{
        .buffer = actualIndexBuf,
        .offset = 0,
        .range  = actualIndexSize
    };
    VkWriteDescriptorSet meshIndexDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 6,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo     = &meshIndexInfo
    };
    writes.push_back(meshIndexDescriptor);

    VkDescriptorBufferInfo meshMaterialInfo{
        .buffer = actualMaterialBuf,
        .offset = 0,
        .range  = actualMaterialSize
    };
    VkWriteDescriptorSet meshMaterialDescriptor{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet_,
        .dstBinding      = 7,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo     = &meshMaterialInfo
    };
    writes.push_back(meshMaterialDescriptor);

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
        // Binding 0: Gaussian TLAS
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
        // Binding 2: Scene Bounds UBO
        {
            .binding    = 2,
            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 3: Mesh TLAS (separate acceleration structure for reflection meshes)
        {
            .binding    = 3,
            .type       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
        },
        // Binding 4: Mesh Vertex Buffer
        {
            .binding    = 4,
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        },
        // Binding 5: Mesh Normal Buffer
        {
            .binding    = 5,
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        },
        // Binding 6: Mesh Index Buffer
        {
            .binding    = 6,
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        },
        // Binding 7: Mesh Material Buffer
        {
            .binding    = 7,
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .count      = 1,
            .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        }
    };

    layout_.create(ctx_->getDevice(), bindings);

    return layout_.layout != VK_NULL_HANDLE;
}


bool RayTraceDescriptorSet::createPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2 },  // binding 0 (Gaussian) + binding 3 (Mesh)
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1 },  // binding 1
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             1 },  // binding 2
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             4 }   // binding 4,5,6,7
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