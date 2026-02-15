#ifndef MESH_BLAS_H
#define MESH_BLAS_H

#include "mesh-data.h"
#include "mesh-buffers.h"
#include "vulkan/vkaccelstruct.h"

#include <vulkan/vulkan_core.h>

#include <vector>


class VkProvider;


namespace vk3dgrt {

class MeshBLAS
{
    bool built = false;

    std::vector<AccelerationStructure> blasList;
    AccelerationStructureBuilder       builder;

public:
    MeshBLAS()  = default;
    ~MeshBLAS() = default;

    MeshBLAS(const MeshBLAS&)            = delete;
    MeshBLAS& operator=(const MeshBLAS&) = delete;
    MeshBLAS(MeshBLAS&&)                 = default;
    MeshBLAS& operator=(MeshBLAS&&)      = default;

    bool build(VkContext* context,
               VkCommandBuffer cmdBuffer,
               const MeshBuffers& meshBuffers);

    bool buildAndSubmit(VkProvider* provider, const MeshBuffers& meshBuffers);

    void cleanup(VkContext* context);

    bool isBuilt() const { return built; }

    uint32_t getBLASCount() const { return static_cast<uint32_t>(blasList.size()); }

    VkDeviceAddress getDeviceAddress(uint32_t meshIndex) const
    {
        return blasList[meshIndex].deviceAddress;
    }
};

}   // namespace vk3dgrt

#endif  // MESH_BLAS_H