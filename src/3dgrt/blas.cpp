#include "blas.h"

#include "buffers.h"
#include "log.h"

#include "vulkan/vkprovider.h"
#include "vulkan/vkerror.h"

#include <glm/glm.hpp>

namespace vk3dgrt {

bool BLAS::build(VkContext* context,
                 VkCommandBuffer cmdBuffer,
                 const GaussianParticleBuffers& buffers)
{
    if (!buffers.isInitialized())
    {
        Log::ERR("BLAS") << "GaussianParticleBuffers not initialized";
        return false;
    }

    // Get buffer addresses and mesh info from GaussianParticleBuffers
    VkDeviceAddress vertexBuffer = buffers.getVertexBufferAddress();
    VkDeviceAddress indexBuffer  = buffers.getIndexBufferAddress();
    uint32_t vertexCount         = buffers.getIcosahedronVertexCount();
    uint32_t triangleCount       = buffers.getIcosahedronTriangleCount();

    return build(context, cmdBuffer, vertexBuffer, indexBuffer, vertexCount, triangleCount);
}


bool BLAS::build(VkContext* context,
                 VkCommandBuffer cmdBuffer,
                 VkDeviceAddress vertexBuffer,
                 VkDeviceAddress indexBuffer,
                 uint32_t vertexCount,
                 uint32_t triangleCount)
{
    if (built)
    {
        Log::ERR("BLAS") << "BLAS already built";
        return false;
    }

    if (vertexBuffer == 0 || indexBuffer == 0)
    {
        Log::ERR("BLAS") << "Invalid buffer addresses";
        return false;
    }

    // Initialize the acceleration structure builder
    builder.init(context);

    // Build BLAS using non-opaque triangles (for Any-Hit shader invocation)
    // This is required for 3DGRT to evaluate Gaussian density in the Any-Hit shader
    constexpr uint32_t vertexStride = sizeof(glm::vec3);  // 12 bytes

    // Build flags (matching nvpro reference):
    //   PREFER_FAST_TRACE: Optimize BVH for trace performance over build speed
    //   ALLOW_COMPACTION:  Enable post-build compaction (reduces memory ~30-50%)
    //   LOW_MEMORY:        Hint to reduce scratch/AS memory at potential quality cost
    //                      (nvpro uses this + compaction for optimal cache behavior)
    blas = builder.buildBlas(
        cmdBuffer,
        vertexBuffer,
        indexBuffer,
        vertexCount,
        vertexStride,
        triangleCount,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR
    );

    if (blas.accelerationStructure == VK_NULL_HANDLE)
    {
        Log::ERR("BLAS") << "Failed to build BLAS";
        return false;
    }

    built = true;

    return true;
}


bool BLAS::buildAndSubmit(VkProvider* provider, const GaussianParticleBuffers& buffers)
{
    VkContext* context = &provider->getContext();

    // Pass 1: Build BLAS with ALLOW_COMPACTION + query compacted size
    VkCommandBuffer cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    bool result = build(context, cmd, buffers);
    if (!result)
    {
        provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);
        builder.releasePendingBuffers();
        return false;
    }

    VkQueryPool queryPool = builder.queryCompactedSize(cmd, blas.accelerationStructure);

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);
    builder.releasePendingBuffers();

    // Read compacted size (GPU has completed at this point)
    VkDeviceSize compactedSize = builder.readCompactedSize(queryPool);
    VkDeviceSize originalSize  = blas.buffer.size;

    // Pass 2: Copy to compacted BLAS
    cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    AccelerationStructure compactBlas = builder.compactBlas(cmd, blas.accelerationStructure, compactedSize);

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);

    // Replace original with compacted
    blas.cleanup(context);
    blas = std::move(compactBlas);

    int savedPct = (originalSize > 0) ? static_cast<int>(100 - (compactedSize * 100 / originalSize)) : 0;
    Log::OK("BLAS") << "Built (compacted "
        << Log::formatMemory(originalSize) << " -> "
        << Log::Color::Bold << Log::formatMemory(compactedSize) << Log::Color::Reset
        << ", " << savedPct << "% saved)";

    return true;
}


void BLAS::cleanup(VkContext* context)
{
    if (!built)
    {
        return;
    }

    blas.cleanup(context);
    built = false;
}

}   // namespace vk3dgrt