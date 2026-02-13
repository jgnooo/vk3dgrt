#ifndef BLAS_H
#define BLAS_H

#include "vulkan/vkaccelstruct.h"

#include <vulkan/vulkan_core.h>


class VkProvider;


namespace vk3dgrt {

class GaussianParticleBuffers;


class BLAS
{
    bool built = false;

    AccelerationStructure        blas;
    AccelerationStructureBuilder builder;

public:
    BLAS()  = default;
    ~BLAS() = default;

    // Disable copy, allow move
    BLAS(const BLAS&)            = delete;
    BLAS& operator=(const BLAS&) = delete;
    BLAS(BLAS&&)                 = default;
    BLAS& operator=(BLAS&&)      = default;

    bool build(VkContext* context,
               VkCommandBuffer cmdBuffer,
               const GaussianParticleBuffers& buffers);

    bool build(VkContext* context,
               VkCommandBuffer cmdBuffer,
               VkDeviceAddress vertexBuffer,
               VkDeviceAddress indexBuffer,
               uint32_t vertexCount,
               uint32_t triangleCount);

    bool buildAndSubmit(VkProvider* provider, const GaussianParticleBuffers& buffers);

    void cleanup(VkContext* context);

    bool isBuilt() const { return built; }

    VkDeviceAddress getDeviceAddress() const { return blas.deviceAddress; }

    VkAccelerationStructureKHR getHandle() const { return blas.accelerationStructure; }

    VkDeviceSize getBufferSize() const { return blas.buffer.size; }
};

}   // namespace vk3dgrt

#endif // BLAS_H