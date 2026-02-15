#ifndef RT_DESCRIPTOR_SET_H
#define RT_DESCRIPTOR_SET_H

#include "vulkan/vkpipeline.h"

#include <vulkan/vulkan_core.h>


struct VkContext;


namespace vk3dgrt {

enum class DescriptorSetType
{
    RAYTRACE = 0   // TLAS, output image, scene bounds
};


class RayTraceDescriptorSet
{
    VkContext* ctx_ = nullptr;

    // Descriptor infrastructure
    DescriptorSetLayout layout_;
    DescriptorAllocator allocator_;
    VkDescriptorSet     descriptorSet_ = VK_NULL_HANDLE;

    // Dirty flag
    bool dirty_ = true;

    // Cached TLAS handle (must stay alive during vkUpdateDescriptorSets)
    VkAccelerationStructureKHR cachedTlasHandle_ = VK_NULL_HANDLE;

    // Bound resource tracking for change detection
    VkAccelerationStructureKHR boundTlas_        = VK_NULL_HANDLE;
    VkImageView                boundOutputView_  = VK_NULL_HANDLE;
    VkBuffer                   boundSceneBounds_ = VK_NULL_HANDLE;

    // Mesh resource tracking (binding 3-6)
    VkAccelerationStructureKHR cachedMeshTlasHandle_ = VK_NULL_HANDLE;
    VkAccelerationStructureKHR boundMeshTlas_        = VK_NULL_HANDLE;
    VkBuffer                   boundMeshVertices_    = VK_NULL_HANDLE;
    VkBuffer                   boundMeshIndices_     = VK_NULL_HANDLE;
    VkBuffer                   boundMeshMaterials_   = VK_NULL_HANDLE;

    // Dummy buffer for unbound mesh descriptors (bindings 4,5,6 placeholder)
    AllocatedBuffer dummyBuffer_;

public:
    RayTraceDescriptorSet()  = default;
    ~RayTraceDescriptorSet() = default;

    // Disable copy, allow move
    RayTraceDescriptorSet(const RayTraceDescriptorSet&)            = delete;
    RayTraceDescriptorSet& operator=(const RayTraceDescriptorSet&) = delete;
    RayTraceDescriptorSet(RayTraceDescriptorSet&&)                 = default;
    RayTraceDescriptorSet& operator=(RayTraceDescriptorSet&&)      = default;

    bool initialize(VkContext* context);

    void cleanup(VkDevice device);

    bool update(VkAccelerationStructureKHR tlas,
                VkImageView outputImageView,
                VkBuffer sceneBoundsBuffer,
                VkDeviceSize sceneBoundsSize,
                VkAccelerationStructureKHR meshTlas    = VK_NULL_HANDLE,
                VkBuffer meshVertexBuffer              = VK_NULL_HANDLE,
                VkDeviceSize meshVertexSize            = 0,
                VkBuffer meshIndexBuffer               = VK_NULL_HANDLE,
                VkDeviceSize meshIndexSize             = 0,
                VkBuffer meshMaterialBuffer            = VK_NULL_HANDLE,
                VkDeviceSize meshMaterialSize          = 0);

    void updateOutputImage(VkImageView newImageView);

    void bind(VkCommandBuffer cmd, VkPipelineLayout layout) const;

    void markDirty() { dirty_ = true; }

    bool isDirty() const { return dirty_; }

    VkDescriptorSetLayout getLayout() const { return layout_.layout; }

    VkDescriptorSet getSet() const { return descriptorSet_; }

private:
    bool createLayout();
    bool createPool();
    bool allocateSet();
};

}   // namespace vk3dgrt

#endif // RT_DESCRIPTOR_SET_H