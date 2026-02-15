#include "mesh-blas.h"
#include "mesh-buffers.h"

#include "log.h"

#include "vulkan/vkprovider.h"
#include "vulkan/vkerror.h"

#include <glm/glm.hpp>


namespace vk3dgrt {

bool MeshBLAS::build(VkContext* context,
                     VkCommandBuffer cmdBuffer,
                     const MeshBuffers& meshBuffers)
{
    if (!meshBuffers.isInitialized())
    {
        Log::ERR("MeshBLAS") << "MeshBuffers not initialized";
        return false;
    }

    if (built)
    {
        Log::ERR("MeshBLAS") << "MeshBLAS already built";
        return false;
    }

    builder.init(context);

    uint32_t meshCount = meshBuffers.getMeshCount();
    blasList.resize(meshCount);

    constexpr uint32_t vertexStride = sizeof(glm::vec3);  // 12 bytes

    // Global vertex buffer BDA (indices are absolute, referencing global positions)
    VkDeviceAddress globalVertexAddr = meshBuffers.getVertexBufferAddress();
    VkDeviceAddress globalIndexAddr  = meshBuffers.getIndexBufferAddress();
    uint32_t        totalVertexCount = meshBuffers.getTotalVertexCount();

    for (uint32_t i = 0; i < meshCount; ++i)
    {
        // Per-mesh index sub-range within the concatenated index buffer
        VkDeviceAddress indexAddr = globalIndexAddr +
            static_cast<VkDeviceSize>(meshBuffers.getIndexOffset(i)) * sizeof(glm::uvec3);

        uint32_t triangleCount = meshBuffers.getTriangleCount(i);

        // Build opaque BLAS (mesh uses closest-hit, not any-hit)
        // Flags match Gaussian BLAS: FAST_TRACE + ALLOW_COMPACTION + LOW_MEMORY
        blasList[i] = builder.buildBlas(
            cmdBuffer,
            globalVertexAddr,
            indexAddr,
            totalVertexCount,
            vertexStride,
            triangleCount,
            VK_FORMAT_R32G32B32_SFLOAT,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
                VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR,
            VK_GEOMETRY_OPAQUE_BIT_KHR
        );

        if (blasList[i].accelerationStructure == VK_NULL_HANDLE)
        {
            Log::ERR("MeshBLAS") << "Failed to build BLAS for mesh " << i;
            return false;
        }
    }

    built = true;

    return true;
}


bool MeshBLAS::buildAndSubmit(VkProvider* provider, const MeshBuffers& meshBuffers)
{
    VkContext* context = &provider->getContext();

    // Pass 1: Build all BLASes + query compacted sizes
    VkCommandBuffer cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    bool result = build(context, cmd, meshBuffers);
    if (!result)
    {
        provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);
        builder.releasePendingBuffers();
        return false;
    }

    uint32_t meshCount = meshBuffers.getMeshCount();
    std::vector<VkQueryPool> queryPools(meshCount);

    for (uint32_t i = 0; i < meshCount; ++i)
    {
        queryPools[i] = builder.queryCompactedSize(cmd, blasList[i].accelerationStructure);
    }

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);
    builder.releasePendingBuffers();

    // Read compacted sizes (GPU has completed at this point)
    std::vector<VkDeviceSize> compactedSizes(meshCount);

    VkDeviceSize totalOriginal  = 0;
    VkDeviceSize totalCompacted = 0;

    for (uint32_t i = 0; i < meshCount; ++i)
    {
        compactedSizes[i] = builder.readCompactedSize(queryPools[i]);
        totalOriginal    += blasList[i].buffer.size;
        totalCompacted   += compactedSizes[i];
    }

    // Pass 2: Compact all BLASes
    cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    std::vector<AccelerationStructure> compactedList(meshCount);
    for (uint32_t i = 0; i < meshCount; ++i)
    {
        compactedList[i] = builder.compactBlas(cmd, blasList[i].accelerationStructure, compactedSizes[i]);
    }

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);

    // Replace originals with compacted
    for (uint32_t i = 0; i < meshCount; ++i)
    {
        blasList[i].cleanup(context);
        blasList[i] = std::move(compactedList[i]);
    }

    int savedPct = (totalOriginal > 0)
        ? static_cast<int>(100 - (totalCompacted * 100 / totalOriginal))
        : 0;

    Log::OK("MeshBLAS") << "Built " << meshCount << " BLAS (compacted "
        << Log::formatMemory(totalOriginal) << " -> "
        << Log::Color::Bold << Log::formatMemory(totalCompacted) << Log::Color::Reset
        << ", " << savedPct << "% saved)";

    return true;
}


void MeshBLAS::cleanup(VkContext* context)
{
    if (!built)
    {
        return;
    }

    for (auto& blas : blasList)
    {
        blas.cleanup(context);
    }

    blasList.clear();
    built = false;
}

}   // namespace vk3dgrt
