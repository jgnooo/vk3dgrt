#include "mesh-buffers.h"

#include "log.h"

#include "vulkan/vkerror.h"

#include <cstring>


namespace vk3dgrt {

bool MeshBuffers::initialize(VkContext* context,
                             const std::vector<MeshInstance>& meshes,
                             VkQueue transferQueue,
                             VkCommandPool transferCommandPool)
{
    if (initialized_)
    {
        Log::ERR("MeshBuffers") << "Already initialized";
        return false;
    }

    if (meshes.empty())
    {
        Log::ERR("MeshBuffers") << "No meshes to upload";
        return false;
    }

    meshCount_ = static_cast<uint32_t>(meshes.size());

    vertexOffsets_.resize(meshCount_);
    indexOffsets_.resize(meshCount_);
    triangleCounts_.resize(meshCount_);

    uint32_t totalVertices  = 0;
    uint32_t totalTriangles = 0;

    for (uint32_t i = 0; i < meshCount_; ++i)
    {
        vertexOffsets_[i]  = totalVertices;
        indexOffsets_[i]   = totalTriangles;
        triangleCounts_[i] = meshes[i].getTriangleCount();

        totalVertices  += meshes[i].getVertexCount();
        totalTriangles += meshes[i].getTriangleCount();
    }

    vertexCount_ = totalVertices;
    indexCount_  = totalTriangles * 3;

    std::vector<glm::vec3> allVertices;
    allVertices.reserve(totalVertices);

    std::vector<glm::vec3> allNormals;
    allNormals.reserve(totalVertices);

    for (const auto& mesh : meshes)
    {
        allVertices.insert(allVertices.end(),
                           mesh.vertices.begin(),
                           mesh.vertices.end());
        allNormals.insert(allNormals.end(),
                          mesh.normals.begin(),
                          mesh.normals.end());
    }

    std::vector<glm::uvec3> allIndices;
    allIndices.reserve(totalTriangles);

    for (uint32_t i = 0; i < meshCount_; ++i)
    {
        uint32_t vertexOffset = vertexOffsets_[i];

        for (const auto& tri : meshes[i].indices)
        {
            allIndices.push_back(glm::uvec3(
                tri.x + vertexOffset,
                tri.y + vertexOffset,
                tri.z + vertexOffset
            ));
        }
    }

    std::vector<MeshMaterialGPU> allMaterials;
    allMaterials.reserve(meshCount_);

    for (uint32_t i = 0; i < meshCount_; ++i)
    {
        allMaterials.push_back(toGPUMaterial(meshes[i].material, indexOffsets_[i]));
    }

    VkDeviceSize vertexDataSize = totalVertices * sizeof(glm::vec3);
    vertexBuffer_.create(context, vertexDataSize, BufferUsage::AS_BUILD_INPUT, false);

    if (!stageAndUpload(context, vertexBuffer_, allVertices.data(),
                        vertexDataSize, transferQueue, transferCommandPool))
    {
        Log::ERR("MeshBuffers") << "Failed to upload vertex buffer";
        return false;
    }

    VkDeviceSize normalDataSize = totalVertices * sizeof(glm::vec3);
    normalBuffer_.create(context, normalDataSize, BufferUsage::STORAGE, false);

    if (!stageAndUpload(context, normalBuffer_, allNormals.data(),
                        normalDataSize, transferQueue, transferCommandPool))
    {
        Log::ERR("MeshBuffers") << "Failed to upload normal buffer";
        return false;
    }

    VkDeviceSize indexDataSize = totalTriangles * sizeof(glm::uvec3);
    indexBuffer_.create(context, indexDataSize, BufferUsage::AS_BUILD_INPUT, false);

    if (!stageAndUpload(context, indexBuffer_, allIndices.data(),
                        indexDataSize, transferQueue, transferCommandPool))
    {
        Log::ERR("MeshBuffers") << "Failed to upload index buffer";
        return false;
    }

    VkDeviceSize materialDataSize = meshCount_ * sizeof(MeshMaterialGPU);
    materialBuffer_.create(context, materialDataSize, BufferUsage::STORAGE, false);

    if (!stageAndUpload(context, materialBuffer_, allMaterials.data(),
                        materialDataSize, transferQueue, transferCommandPool))
    {
        Log::ERR("MeshBuffers") << "Failed to upload material buffer";
        return false;
    }

    initialized_ = true;

    VkDeviceSize totalSize = vertexDataSize + normalDataSize + indexDataSize + materialDataSize;
    Log::OK("MeshBuffers") << "Uploaded " << meshCount_ << " meshes ("
        << totalVertices << " verts, " << totalTriangles << " tris, "
        << Log::formatMemory(totalSize) << ")";

    return true;
}


void MeshBuffers::cleanup(VmaAllocator allocator)
{
    if (!initialized_)
    {
        return;
    }

    vertexBuffer_.cleanup(allocator);
    normalBuffer_.cleanup(allocator);
    indexBuffer_.cleanup(allocator);
    materialBuffer_.cleanup(allocator);

    vertexOffsets_.clear();
    indexOffsets_.clear();
    triangleCounts_.clear();

    initialized_  = false;
    vertexCount_  = 0;
    indexCount_   = 0;
    meshCount_    = 0;
}


bool MeshBuffers::stageAndUpload(VkContext* context,
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
        true
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

    // Wait for transfer to complete
    constexpr uint64_t kTransferTimeoutNs = 5'000'000'000;  // 5 seconds
    VkResult waitResult = vkWaitForFences(device, 1, &fence, VK_TRUE, kTransferTimeoutNs);

    if (waitResult == VK_TIMEOUT)
    {
        Log::ERR("MeshBuffers") << "Transfer timeout";
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
