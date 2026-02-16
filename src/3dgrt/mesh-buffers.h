#ifndef MESH_BUFFERS_H
#define MESH_BUFFERS_H

#include "mesh-data.h"

#include "vulkan/vkbuffer.h"
#include "vulkan/vkcontext.h"

#include <cstdint>
#include <vector>


namespace vk3dgrt {

class MeshBuffers
{
    bool     initialized_  = false;
    uint32_t vertexCount_  = 0;
    uint32_t indexCount_   = 0;
    uint32_t meshCount_    = 0;

    AllocatedBuffer vertexBuffer_;
    AllocatedBuffer normalBuffer_;
    AllocatedBuffer indexBuffer_;
    AllocatedBuffer materialBuffer_;

    std::vector<uint32_t> vertexOffsets_;
    std::vector<uint32_t> indexOffsets_;
    std::vector<uint32_t> triangleCounts_;

public:
    MeshBuffers()  = default;
    ~MeshBuffers() = default;

    MeshBuffers(const MeshBuffers&)            = delete;
    MeshBuffers& operator=(const MeshBuffers&) = delete;
    MeshBuffers(MeshBuffers&&)                 = default;
    MeshBuffers& operator=(MeshBuffers&&)      = default;

    bool initialize(VkContext* context,
                    const std::vector<MeshInstance>& meshes,
                    VkQueue transferQueue,
                    VkCommandPool transferCommandPool);

    void cleanup(VmaAllocator allocator);

    bool isInitialized() const { return initialized_; }

    uint32_t getMeshCount() const { return meshCount_; }
    uint32_t getTotalVertexCount() const { return vertexCount_; }
    uint32_t getTotalIndexCount() const { return indexCount_; }

    VkBuffer getVertexBufferHandle() const { return vertexBuffer_.buffer; }
    VkBuffer getNormalBufferHandle() const { return normalBuffer_.buffer; }
    VkBuffer getIndexBufferHandle() const { return indexBuffer_.buffer; }
    VkBuffer getMaterialBufferHandle() const { return materialBuffer_.buffer; }

    VkDeviceSize getVertexBufferSize() const { return vertexBuffer_.size; }
    VkDeviceSize getNormalBufferSize() const { return normalBuffer_.size; }
    VkDeviceSize getIndexBufferSize() const { return indexBuffer_.size; }
    VkDeviceSize getMaterialBufferSize() const { return materialBuffer_.size; }

    VkDeviceAddress getVertexBufferAddress() const { return vertexBuffer_.deviceAddress; }
    VkDeviceAddress getNormalBufferAddress() const { return normalBuffer_.deviceAddress; }
    VkDeviceAddress getIndexBufferAddress() const { return indexBuffer_.deviceAddress; }

    uint32_t getVertexOffset(uint32_t meshIndex) const { return vertexOffsets_[meshIndex]; }
    uint32_t getIndexOffset(uint32_t meshIndex) const { return indexOffsets_[meshIndex]; }
    uint32_t getTriangleCount(uint32_t meshIndex) const { return triangleCounts_[meshIndex]; }

private:
    bool stageAndUpload(VkContext* context,
                        AllocatedBuffer& dstBuffer,
                        const void* data,
                        VkDeviceSize dataSize,
                        VkQueue transferQueue,
                        VkCommandPool commandPool);
};

}   // namespace vk3dgrt

#endif  // MESH_BUFFERS_H