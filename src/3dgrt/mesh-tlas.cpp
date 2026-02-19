#include "mesh-tlas.h"

#include "log.h"

#include "vulkan/vkprovider.h"
#include "vulkan/vkerror.h"


namespace vk3dgrt {

bool MeshTLAS::build(VkContext* context,
                     VkCommandBuffer cmdBuffer,
                     const std::vector<MeshTLASInstance>& meshInstances)
{
    if (built)
    {
        Log::ERR("MeshTLAS") << "MeshTLAS already built";
        return false;
    }

    if (meshInstances.empty())
    {
        Log::ERR("MeshTLAS") << "No mesh instances to build TLAS";
        return false;
    }

    builder.init(context);
    instanceCount_ = static_cast<uint32_t>(meshInstances.size());

    // Build flags: ALLOW_UPDATE enables in-place transform updates
    VkBuildAccelerationStructureFlagsKHR flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    // Create persistent double-buffered instance buffers
    VkDeviceSize instanceBufferSize =
        sizeof(VkAccelerationStructureInstanceKHR) * instanceCount_;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        instanceBuffers_[i].create(context, instanceBufferSize,
                                   BufferUsage::AS_BUILD_INPUT, true);
    }

    // Upload initial instance data to buffer[0]
    uploadInstances(instanceBuffers_[0], meshInstances);

    // Build TLAS using buffer[0]
    tlas = builder.buildTlas(cmdBuffer,
                             instanceBuffers_[0].deviceAddress,
                             instanceCount_,
                             flags);

    if (tlas.accelerationStructure == VK_NULL_HANDLE)
    {
        Log::ERR("MeshTLAS") << "Failed to build MeshTLAS";
        return false;
    }

    // Query update scratch size for persistent scratch allocation
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data            = {.deviceAddress = instanceBuffers_[0].deviceAddress}
    };

    VkAccelerationStructureGeometryKHR asGeometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry     = {.instances = instancesData},
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR
    };

    VkAccelerationStructureBuildGeometryInfoKHR sizeQuery{
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags         = flags,
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
        .geometryCount = 1,
        .pGeometries   = &asGeometry
    };

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    vkGetAccelerationStructureBuildSizesKHR_(
        context->getDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &sizeQuery,
        &instanceCount_,
        &sizeInfo
    );

    // Create persistent double-buffered scratch buffers
    VkDeviceSize scratchSize      = sizeInfo.updateScratchSize;
    uint32_t     scratchAlignment =
        context->rtProps.minAccelerationStructureScratchOffsetAlignment;
    scratchSize =
        ((scratchSize + scratchAlignment - 1) / scratchAlignment) * scratchAlignment;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        updateScratches_[i].create(context, scratchSize,
                                   BufferUsage::STORAGE, false);
    }

    built = true;

    Log::OK("MeshTLAS") << "Built (" << meshInstances.size()
                         << " instances, update-ready)";

    return true;
}


bool MeshTLAS::buildAndSubmit(VkProvider* provider,
                              const std::vector<MeshTLASInstance>& meshInstances)
{
    VkContext* context = &provider->getContext();
    VkCommandBuffer cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    bool result = build(context, cmd, meshInstances);

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);
    builder.releasePendingBuffers();

    return result;
}


void MeshTLAS::recordUpdate(VkCommandBuffer cmdBuffer,
                             uint32_t frameIndex,
                             const std::vector<MeshTLASInstance>& meshInstances)
{
    if (!built)
    {
        return;
    }

    if (static_cast<uint32_t>(meshInstances.size()) != instanceCount_)
    {
        return;
    }

    uint32_t fi = frameIndex % kMaxFramesInFlight;

    // Upload new instance data to double-buffered persistent buffer
    uploadInstances(instanceBuffers_[fi], meshInstances);

    // Build flags must match the original build
    VkBuildAccelerationStructureFlagsKHR flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    // Setup geometry info pointing to the current frame's instance buffer
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data            = {.deviceAddress = instanceBuffers_[fi].deviceAddress}
    };

    VkAccelerationStructureGeometryKHR asGeometry{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry     = {.instances = instancesData},
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR
    };

    // In-place TLAS update (src == dst)
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type                     = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags                    = flags,
        .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
        .srcAccelerationStructure = tlas.accelerationStructure,
        .dstAccelerationStructure = tlas.accelerationStructure,
        .geometryCount            = 1,
        .pGeometries              = &asGeometry
    };

    buildInfo.scratchData.deviceAddress = updateScratches_[fi].deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{
        .primitiveCount  = instanceCount_,
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

    // Memory barrier: AS update write -> ray tracing read
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
}


void MeshTLAS::cleanup(VkContext* context)
{
    if (!built)
    {
        return;
    }

    // Cleanup persistent double-buffered resources
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        instanceBuffers_[i].cleanup(context->getAllocator());
        updateScratches_[i].cleanup(context->getAllocator());
    }
    instanceCount_ = 0;

    tlas.cleanup(context);
    built = false;
}


std::vector<AccelerationStructureInstance> MeshTLAS::generateInstances(const std::vector<MeshTLASInstance>& meshInstances) const
{
    std::vector<AccelerationStructureInstance> instances;
    instances.reserve(meshInstances.size());

    for (const auto& meshInst : meshInstances)
    {
        // Convert glm::mat4 (column-major) to VkTransformMatrixKHR (3x4 row-major)
        VkTransformMatrixKHR vkTransform{};
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                vkTransform.matrix[row][col] = meshInst.transform[col][row];
            }
        }

        AccelerationStructureInstance instance{};
        instance.transform                              = vkTransform;
        instance.instanceCustomIndex                    = meshInst.meshIndex;
        instance.mask                                   = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 1;    // Hit Group 1 (mesh closest-hit)
        instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference         = meshInst.blasAddress;

        instances.push_back(instance);
    }

    return instances;
}


void MeshTLAS::uploadInstances(AllocatedBuffer& buffer,
                                const std::vector<MeshTLASInstance>& meshInstances) const
{
    std::vector<VkAccelerationStructureInstanceKHR> vkInstances(meshInstances.size());

    for (size_t i = 0; i < meshInstances.size(); ++i)
    {
        VkTransformMatrixKHR vkTransform{};
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                vkTransform.matrix[row][col] = meshInstances[i].transform[col][row];
            }
        }

        vkInstances[i].transform                              = vkTransform;
        vkInstances[i].instanceCustomIndex                    = meshInstances[i].meshIndex;
        vkInstances[i].mask                                   = 0xFF;
        vkInstances[i].instanceShaderBindingTableRecordOffset = 1;
        vkInstances[i].flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstances[i].accelerationStructureReference         = meshInstances[i].blasAddress;
    }

    buffer.upload(vkInstances.data(),
                  sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size());
}

}   // namespace vk3dgrt
