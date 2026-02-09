#ifndef VKACCELSTRUCT_H
#define VKACCELSTRUCT_H

#include "vkbuffer.h"

#include <vulkan/vulkan_core.h>

#include <vector>
#include <cstdint>


struct VkContext;


struct AccelerationStructure
{
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    AllocatedBuffer            buffer;
    VkDeviceAddress            deviceAddress = 0;

    void cleanup(VkContext* context);
};


struct AccelerationStructureInstance
{
    VkTransformMatrixKHR       transform;
    uint32_t                   instanceCustomIndex;
    uint32_t                   mask;
    uint32_t                   instanceShaderBindingTableRecordOffset;
    VkGeometryInstanceFlagsKHR flags;
    VkDeviceAddress            accelerationStructureReference;
};


struct AccelerationStructureBuilder
{
    VkContext* context = nullptr;

    void init(VkContext* ctx);

    AccelerationStructure buildBlas(VkCommandBuffer cmdBuffer,
                                    VkDeviceAddress vertexBuffer,
                                    VkDeviceAddress indexBuffer,
                                    uint32_t vertexCount,
                                    uint32_t vertexStride,
                                    uint32_t triangleCount,
                                    VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                                    VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    AccelerationStructure buildTlas(VkCommandBuffer cmdBuffer,
                                    const std::vector<AccelerationStructureInstance>& instances,
                                    VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    AccelerationStructure buildTlas(VkCommandBuffer cmdBuffer,
                                    VkDeviceAddress instanceBuffer,
                                    uint32_t instanceCount,
                                    VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

private:
    AllocatedBuffer createScratchBuffer(VkDeviceSize size);
};

#endif // VKACCELSTRUCT_H