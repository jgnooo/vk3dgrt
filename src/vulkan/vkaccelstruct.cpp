#include "vkaccelstruct.h"
#include "vkcontext.h"
#include "vkerror.h"


// --------------------------------------------------- //
//  AccelerationStructure Implementation
// --------------------------------------------------- //
void AccelerationStructure::cleanup(VkContext* context)
{
    if (accelerationStructure != VK_NULL_HANDLE)
    {
        vkDestroyAccelerationStructureKHR_(context->getDevice(), accelerationStructure, nullptr);
        accelerationStructure = VK_NULL_HANDLE;
    }

    buffer.cleanup(context->getAllocator());
    deviceAddress = 0;
}


// --------------------------------------------------- //
//  AccelerationStructureBuilder Implementation
// --------------------------------------------------- //
void AccelerationStructureBuilder::init(VkContext* ctx)
{
    context = ctx;
}


AllocatedBuffer AccelerationStructureBuilder::createScratchBuffer(VkDeviceSize size)
{
    AllocatedBuffer scratchBuffer;

    VkBufferCreateInfo bufferInfo{
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocInfo{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    VK_CHECK(vmaCreateBuffer(
        context->getAllocator(),
        &bufferInfo,
        &allocInfo,
        &scratchBuffer.buffer,
        &scratchBuffer.allocation,
        &scratchBuffer.allocInfo
    ));

    VkBufferDeviceAddressInfo addressInfo{
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = scratchBuffer.buffer
    };
    scratchBuffer.deviceAddress = vkGetBufferDeviceAddressKHR_(context->getDevice(), &addressInfo);
    scratchBuffer.size = size;

    return scratchBuffer;
}


AccelerationStructure AccelerationStructureBuilder::buildBlas(VkCommandBuffer cmdBuffer,
                                                              VkDeviceAddress vertexBuffer,
                                                              VkDeviceAddress indexBuffer,
                                                              uint32_t vertexCount,
                                                              uint32_t vertexStride,
                                                              uint32_t triangleCount,
                                                              VkFormat vertexFormat,
                                                              VkBuildAccelerationStructureFlagsKHR flags)
{
    AccelerationStructure as;

    // Configure triangle geometry (non-opaque for any-hit shader invocation)
    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = vertexFormat,
        .vertexData   = {.deviceAddress = vertexBuffer},
        .vertexStride = vertexStride,
        .maxVertex    = vertexCount - 1,
        .indexType    = VK_INDEX_TYPE_UINT32,
        .indexData    = {.deviceAddress = indexBuffer}
    };

    VkAccelerationStructureGeometryKHR asGeometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry     = {.triangles = trianglesData},
        .flags        = 0  // Non-opaque: Any-Hit shader will be invoked
    };

    // Get build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags         = flags,
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries   = &asGeometry
    };

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    vkGetAccelerationStructureBuildSizesKHR_(
        context->getDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &triangleCount,
        &sizeInfo
    );

    // Create AS buffer
    as.buffer.create(
        context,
        sizeInfo.accelerationStructureSize,
        BufferUsage::ACCELERATION_STRUCTURE,
        false
    );

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = as.buffer.buffer,
        .size   = sizeInfo.accelerationStructureSize,
        .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };

    VK_CHECK(vkCreateAccelerationStructureKHR_(
        context->getDevice(),
        &createInfo,
        nullptr,
        &as.accelerationStructure
    ));

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = as.accelerationStructure
    };
    as.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR_(context->getDevice(), &addressInfo);

    // Create scratch buffer
    VkDeviceSize scratchSize      = sizeInfo.buildScratchSize;
    uint32_t     scratchAlignment = context->rtProps.minAccelerationStructureScratchOffsetAlignment;
    scratchSize = ((scratchSize + scratchAlignment - 1) / scratchAlignment) * scratchAlignment;

    AllocatedBuffer scratchBuffer = createScratchBuffer(scratchSize);

    // Build the acceleration structure
    buildInfo.dstAccelerationStructure  = as.accelerationStructure;
    buildInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{
        .primitiveCount  = triangleCount,
        .primitiveOffset = 0,
        .firstVertex     = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    vkCmdBuildAccelerationStructuresKHR_(
        cmdBuffer,
        1,
        &buildInfo,
        &pRangeInfo
    );

    // Memory barrier
    VkMemoryBarrier barrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );

    scratchBuffer.cleanup(context->getAllocator());

    return as;
}


AccelerationStructure AccelerationStructureBuilder::buildTlas(VkCommandBuffer cmdBuffer,
                                                              const std::vector<AccelerationStructureInstance>& instances,
                                                              VkBuildAccelerationStructureFlagsKHR flags)
{
    // Create instance buffer
    VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();

    AllocatedBuffer instanceBuffer;
    instanceBuffer.create(
        context,
        instanceBufferSize,
        BufferUsage::AS_BUILD_INPUT,
        true
    );

    // Copy instance data
    std::vector<VkAccelerationStructureInstanceKHR> vkInstances(instances.size());
    for (size_t i = 0; i < instances.size(); ++i)
    {
        vkInstances[i].transform                              = instances[i].transform;
        vkInstances[i].instanceCustomIndex                    = instances[i].instanceCustomIndex;
        vkInstances[i].mask                                   = instances[i].mask;
        vkInstances[i].instanceShaderBindingTableRecordOffset = instances[i].instanceShaderBindingTableRecordOffset;
        vkInstances[i].flags                                  = instances[i].flags;
        vkInstances[i].accelerationStructureReference         = instances[i].accelerationStructureReference;
    }

    instanceBuffer.upload(vkInstances.data(), instanceBufferSize);

    AccelerationStructure as = buildTlas(
        cmdBuffer,
        instanceBuffer.deviceAddress,
        static_cast<uint32_t>(instances.size()),
        flags
    );

    instanceBuffer.cleanup(context->getAllocator());

    return as;
}


AccelerationStructure AccelerationStructureBuilder::buildTlas(VkCommandBuffer cmdBuffer,
                                                              VkDeviceAddress instanceBufferAddress,
                                                              uint32_t instanceCount,
                                                              VkBuildAccelerationStructureFlagsKHR flags)
{
    AccelerationStructure as;

    // Configure instance geometry
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data            = {.deviceAddress = instanceBufferAddress}
    };

    VkAccelerationStructureGeometryKHR asGeometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry     = {.instances = instancesData},
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR
    };

    // Get build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags         = flags,
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries   = &asGeometry
    };

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    vkGetAccelerationStructureBuildSizesKHR_(
        context->getDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &sizeInfo
    );

    // Create AS buffer
    as.buffer.create(
        context,
        sizeInfo.accelerationStructureSize,
        BufferUsage::ACCELERATION_STRUCTURE,
        false
    );

    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = as.buffer.buffer,
        .size   = sizeInfo.accelerationStructureSize,
        .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
    };

    VK_CHECK(vkCreateAccelerationStructureKHR_(
        context->getDevice(),
        &createInfo,
        nullptr,
        &as.accelerationStructure
    ));

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = as.accelerationStructure
    };
    as.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR_(context->getDevice(), &addressInfo);

    // Create scratch buffer
    VkDeviceSize scratchSize      = sizeInfo.buildScratchSize;
    uint32_t     scratchAlignment = context->rtProps.minAccelerationStructureScratchOffsetAlignment;
    scratchSize = ((scratchSize + scratchAlignment - 1) / scratchAlignment) * scratchAlignment;

    AllocatedBuffer scratchBuffer = createScratchBuffer(scratchSize);

    // Build the acceleration structure
    buildInfo.dstAccelerationStructure  = as.accelerationStructure;
    buildInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{
        .primitiveCount  = instanceCount,
        .primitiveOffset = 0,
        .firstVertex     = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    vkCmdBuildAccelerationStructuresKHR_(
        cmdBuffer,
        1,
        &buildInfo,
        &pRangeInfo
    );

    // Memory barrier
    VkMemoryBarrier barrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );

    scratchBuffer.cleanup(context->getAllocator());

    return as;
}