#include "buffers.h"

#include "vulkan/vkerror.h"

#include <iostream>


namespace vk3dgrt {

bool GaussianParticleBuffers::initialize(VkContext* context,
                                      const GaussianParticleData& data,
                                      VkQueue transferQueue,
                                      VkCommandPool transferCommandPool)
{
    if (initialized)
    {
        std::cerr << "[GaussianParticleBuffers] Already initialized" << std::endl;
        return false;
    }

    if (data.particles.empty())
    {
        std::cerr << "[GaussianParticleBuffers] No particles to upload" << std::endl;
        return false;
    }

    particleCount = static_cast<uint32_t>(data.particles.size());

    // 1. Upload GaussianParticle data
    if (!uploadParticleData(context, data, transferQueue, transferCommandPool))
    {
        std::cerr << "[GaussianParticleBuffers] Failed to upload particle data" << std::endl;
        return false;
    }

    // 2. Upload SH coefficients (if available)
    if (!data.shCoeffsDC.empty())
    {
        if (!uploadSHData(context, data, transferQueue, transferCommandPool))
        {
            std::cerr << "[GaussianParticleBuffers] Failed to upload SH data" << std::endl;
            return false;
        }
        hasSH    = true;
        shDegree = data.shDegree;
    }
    else
    {
        hasSH    = false;
        shDegree = 0;
    }

    // 3. Upload Icosahedron mesh (shared by all instances)
    if (!uploadIcosahedronData(context, transferQueue, transferCommandPool))
    {
        std::cerr << "[GaussianParticleBuffers] Failed to upload icosahedron data" << std::endl;
        return false;
    }

    initialized = true;

    return true;
}


void GaussianParticleBuffers::cleanup(VmaAllocator allocator)
{
    if (!initialized)
    {
        return;
    }

    positionBuffer.cleanup(allocator);
    colorBuffer.cleanup(allocator);
    quaternionBuffer.cleanup(allocator);
    scaleBuffer.cleanup(allocator);
    shBuffer.cleanup(allocator);
    icosahedronVertexBuffer.cleanup(allocator);
    icosahedronIndexBuffer.cleanup(allocator);

    initialized   = false;
    particleCount = 0;
    hasSH         = false;
    shDegree      = 0;
}


bool GaussianParticleBuffers::uploadParticleData(
    VkContext* context,
    const GaussianParticleData& data,
    VkQueue transferQueue,
    VkCommandPool commandPool)
{
    size_t count = data.particles.size();

    // Extract SoA data from AoS particles
    std::vector<glm::vec3> positions(count);
    std::vector<glm::vec4> colors(count);
    std::vector<glm::vec4> quaternions(count);
    std::vector<glm::vec3> scales(count);

    const bool hasDC = !data.shCoeffsDC.empty();

    for (size_t i = 0; i < count; ++i)
    {
        const GaussianParticle& p = data.particles[i];
        positions[i]  = p.position;
        quaternions[i] = p.quaternion;
        scales[i]     = p.scale;

        // Pre-compute DC color: color = clamp(SH_C0 * shDC + 0.5, 0, 1)
        // This eliminates 24 uint32 reads + full SH math per hit in the shader
        // (1 vec4 read = 16 bytes vs 24 uint32 reads = 96 bytes per particle)
        if (hasDC)
        {
            glm::vec3 dc = SH_C0 * data.shCoeffsDC[i] + glm::vec3(0.5f);
            dc = glm::clamp(dc, glm::vec3(0.0f), glm::vec3(1.0f));
            colors[i] = glm::vec4(dc, p.opacity);
        }
        else
        {
            colors[i] = glm::vec4(1.0f, 1.0f, 1.0f, p.opacity);
        }
    }

    // Create and upload position buffer (vec3 per particle)
    VkDeviceSize posSize = count * sizeof(glm::vec3);
    positionBuffer.create(context, posSize, BufferUsage::STORAGE, false);
    if (!stageAndUpload(context, positionBuffer, positions.data(), posSize, transferQueue, commandPool))
    {
        return false;
    }

    // Create and upload color buffer (vec4 per particle: DC_R, DC_G, DC_B, opacity)
    VkDeviceSize colorSize = count * sizeof(glm::vec4);
    colorBuffer.create(context, colorSize, BufferUsage::STORAGE, false);
    if (!stageAndUpload(context, colorBuffer, colors.data(), colorSize, transferQueue, commandPool))
    {
        return false;
    }

    // Create and upload quaternion buffer (vec4 per particle)
    VkDeviceSize quatSize = count * sizeof(glm::vec4);
    quaternionBuffer.create(context, quatSize, BufferUsage::STORAGE, false);
    if (!stageAndUpload(context, quaternionBuffer, quaternions.data(), quatSize, transferQueue, commandPool))
    {
        return false;
    }

    // Create and upload scale buffer (vec3 per particle)
    VkDeviceSize scaleSize = count * sizeof(glm::vec3);
    scaleBuffer.create(context, scaleSize, BufferUsage::STORAGE, false);
    if (!stageAndUpload(context, scaleBuffer, scales.data(), scaleSize, transferQueue, commandPool))
    {
        return false;
    }

    return true;
}


bool GaussianParticleBuffers::uploadSHData(VkContext* context,
                                           const GaussianParticleData& data,
                                           VkQueue transferQueue,
                                           VkCommandPool commandPool)
{
    // SH data is packed as fp16 (half-float) for 50% bandwidth savings.
    // Layout: 48 half-floats per particle → 24 uint32 per particle
    // Each uint32 holds 2 packed fp16 values via glm::packHalf2x16().

    if (!data.shCoeffsFull.empty())
    {
        // Convert float32 → packed fp16 (uint32)
        // 48 floats per particle → 24 uint32 per particle
        size_t floatCount      = data.shCoeffsFull.size();
        size_t packedCount     = (floatCount + 1) / 2;  // ceil(floatCount / 2)
        std::vector<uint32_t> packedData(packedCount);

        for (size_t i = 0; i < packedCount; ++i)
        {
            float v0 = data.shCoeffsFull[i * 2];
            float v1 = (i * 2 + 1 < floatCount) ? data.shCoeffsFull[i * 2 + 1] : 0.0f;
            packedData[i] = glm::packHalf2x16(glm::vec2(v0, v1));
        }

        VkDeviceSize dataSize    = packedCount * sizeof(uint32_t);
        VkDeviceSize fp32Size    = floatCount * sizeof(float);

        // Create device-local storage buffer
        shBuffer.create(
            context,
            dataSize,
            BufferUsage::STORAGE,
            false  // Device-local
        );

        // Stage and upload
        return stageAndUpload(
            context,
            shBuffer,
            packedData.data(),
            dataSize,
            transferQueue,
            commandPool
        );
    }
    else if (!data.shCoeffsDC.empty())
    {
        // DC only: pack 3 floats per particle as fp16
        size_t floatCount  = data.shCoeffsDC.size() * 3;
        size_t packedCount = (floatCount + 1) / 2;
        std::vector<uint32_t> packedData(packedCount);

        const float* rawDC = reinterpret_cast<const float*>(data.shCoeffsDC.data());
        for (size_t i = 0; i < packedCount; ++i)
        {
            float v0 = rawDC[i * 2];
            float v1 = (i * 2 + 1 < floatCount) ? rawDC[i * 2 + 1] : 0.0f;
            packedData[i] = glm::packHalf2x16(glm::vec2(v0, v1));
        }

        VkDeviceSize dataSize = packedCount * sizeof(uint32_t);

        shBuffer.create(
            context,
            dataSize,
            BufferUsage::STORAGE,
            false
        );

        return stageAndUpload(
            context,
            shBuffer,
            packedData.data(),
            dataSize,
            transferQueue,
            commandPool
        );
    }

    return true;  // No SH data to upload
}


bool GaussianParticleBuffers::uploadIcosahedronData(VkContext* context,
                                                    VkQueue transferQueue,
                                                    VkCommandPool commandPool)
{
    // Vertex buffer
    VkDeviceSize vertexDataSize = icosahedron.getVertexDataSize();
    icosahedronVertexBuffer.create(
        context,
        vertexDataSize,
        BufferUsage::VERTEX,
        false  // Device-local
    );

    if (!stageAndUpload(
            context,
            icosahedronVertexBuffer,
            icosahedron.getVertexData(),
            vertexDataSize,
            transferQueue,
            commandPool))
    {
        return false;
    }

    // Index buffer
    VkDeviceSize indexDataSize = icosahedron.getIndexDataSize();
    icosahedronIndexBuffer.create(
        context,
        indexDataSize,
        BufferUsage::INDEX,
        false  // Device-local
    );

    if (!stageAndUpload(
            context,
            icosahedronIndexBuffer,
            icosahedron.getIndexData(),
            indexDataSize,
            transferQueue,
            commandPool))
    {
        return false;
    }

    return true;
}


bool GaussianParticleBuffers::stageAndUpload(VkContext* context,
                                             AllocatedBuffer& dstBuffer,
                                             const void* data,
                                             VkDeviceSize dataSize,
                                             VkQueue transferQueue,
                                             VkCommandPool commandPool)
{
    VkDevice device = context->getDevice();

    // Create staging buffer (host visible)
    AllocatedBuffer stagingBuffer;
    stagingBuffer.create(
        context,
        dataSize,
        BufferUsage::STAGING,
        true  // Host visible
    );

    // Copy data to staging buffer
    stagingBuffer.upload(data, dataSize, 0);

    // Create command buffer for transfer
    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmdBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer));

    // Begin command buffer (one-time submit)
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));

    // Record copy command
    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = dataSize
    };
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer.buffer, dstBuffer.buffer, 1, &copyRegion);

    // End command buffer
    VK_CHECK(vkEndCommandBuffer(cmdBuffer));

    // Create fence for synchronization
    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0
    };

    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

    // Submit transfer command
    VkSubmitInfo submitInfo{
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmdBuffer
    };

    VK_CHECK(vkQueueSubmit(transferQueue, 1, &submitInfo, fence));

    // Wait for transfer to complete (with timeout)
    constexpr uint64_t kTransferTimeoutNs = 5'000'000'000;  // 5 seconds
    VkResult waitResult = vkWaitForFences(device, 1, &fence, VK_TRUE, kTransferTimeoutNs);

    if (waitResult == VK_TIMEOUT)
    {
        std::cerr << "[GaussianParticleBuffers] Transfer timeout" << std::endl;
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &cmdBuffer);
        stagingBuffer.cleanup(context->getAllocator());
        return false;
    }

    VK_CHECK(waitResult);

    // Cleanup
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &cmdBuffer);
    stagingBuffer.cleanup(context->getAllocator());

    return true;
}

}   // namespace vk3dgrt