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
    AllocatedBuffer              instanceBuffer;

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

    void cleanup(VkContext* context);

    bool isBuilt() const { return built; }

    VkAccelerationStructureKHR getHandle() const { return tlas.accelerationStructure; }

private:
    std::vector<AccelerationStructureInstance> generateInstances(const std::vector<MeshTLASInstance>& meshInstances) const;
};

}   // namespace vk3dgrt

#endif  // MESH_TLAS_H