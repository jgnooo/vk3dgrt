#ifndef MESH_TLAS_H
#define MESH_TLAS_H

#include "mesh-data.h"
#include "mesh-blas.h"
#include "vulkan/vkaccelstruct.h"
#include "vulkan/vkcontext.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>


class VkProvider;


namespace vk3dgrt {

struct MeshTLASInstance
{
    VkDeviceAddress blasAddress;
    glm::mat4       transform;
    uint32_t        meshIndex;
};


class MeshTLAS
{
    bool built = false;

    AccelerationStructure        tlas;
    AccelerationStructureBuilder builder;

    // Double-buffered persistent resources for frame-in-flight safe updates.
    // Avoids per-frame allocation and vkDeviceWaitIdle stalls.
    static constexpr uint32_t kMaxFramesInFlight = 2;
    AllocatedBuffer instanceBuffers_[kMaxFramesInFlight];
    AllocatedBuffer updateScratches_[kMaxFramesInFlight];
    uint32_t        instanceCount_ = 0;

public:
    MeshTLAS()  = default;
    ~MeshTLAS() = default;

    // Disable copy, allow move
    MeshTLAS(const MeshTLAS&)            = delete;
    MeshTLAS& operator=(const MeshTLAS&) = delete;
    MeshTLAS(MeshTLAS&&)                 = default;
    MeshTLAS& operator=(MeshTLAS&&)      = default;

    bool build(VkContext* context,
               VkCommandBuffer cmdBuffer,
               const std::vector<MeshTLASInstance>& meshInstances);

    bool buildAndSubmit(VkProvider* provider,
                        const std::vector<MeshTLASInstance>& meshInstances);

    // Record in-place TLAS update into command buffer (no vkDeviceWaitIdle).
    // Uses double-buffered persistent resources for frame-in-flight safety.
    void recordUpdate(VkCommandBuffer cmdBuffer,
                      uint32_t frameIndex,
                      const std::vector<MeshTLASInstance>& meshInstances);

    void cleanup(VkContext* context);

    bool isBuilt() const { return built; }

    VkAccelerationStructureKHR getHandle() const { return tlas.accelerationStructure; }

private:
    std::vector<AccelerationStructureInstance> generateInstances(const std::vector<MeshTLASInstance>& meshInstances) const;

    void uploadInstances(AllocatedBuffer& buffer,
                         const std::vector<MeshTLASInstance>& meshInstances) const;
};

}   // namespace vk3dgrt

#endif  // MESH_TLAS_H
