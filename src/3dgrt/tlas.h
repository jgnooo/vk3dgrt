#ifndef TLAS_H
#define TLAS_H

#include "data.h"
#include "vulkan/vkaccelstruct.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>


class VkProvider;


namespace vk3dgrt {

class TLAS
{
    bool                         built             = false;
    uint32_t                     instanceCount     = 0;
    VkDeviceAddress              cachedBlasAddress = 0;

    AccelerationStructure        tlas;
    AccelerationStructureBuilder builder;

    uint32_t                     consecutiveUpdates = 0;
    static constexpr uint32_t    MAX_CONSECUTIVE_UPDATES = 15;

public:
    TLAS()  = default;
    ~TLAS() = default;

    // Disable copy, allow move
    TLAS(const TLAS&)            = delete;
    TLAS& operator=(const TLAS&) = delete;
    TLAS(TLAS&&)                 = default;
    TLAS& operator=(TLAS&&)      = default;

    bool build(VkContext* context,
               VkCommandBuffer cmdBuffer,
               const GaussianParticleData& gaussianParticleData,
               VkDeviceAddress blasDeviceAddress,
               float minAlpha    = DEFAULT_MIN_ALPHA,
               int   kernelDegree = DEFAULT_KERNEL_DEGREE);

    bool rebuild(VkContext* context,
                 VkCommandBuffer cmdBuffer,
                 const GaussianParticleData& gaussianParticleData,
                 float minAlpha     = DEFAULT_MIN_ALPHA,
                 int   kernelDegree = DEFAULT_KERNEL_DEGREE);

    bool buildAndSubmit(VkProvider* provider,
                        const GaussianParticleData& gaussianParticleData,
                        VkDeviceAddress blasDeviceAddress,
                        float minAlpha     = DEFAULT_MIN_ALPHA,
                        int   kernelDegree = DEFAULT_KERNEL_DEGREE);

    bool rebuildAndSubmit(VkProvider* provider,
                          const GaussianParticleData& gaussianParticleData,
                          float minAlpha     = DEFAULT_MIN_ALPHA,
                          int   kernelDegree = DEFAULT_KERNEL_DEGREE);

    void cleanup(VkContext* context);

    bool isBuilt() const { return built; }

    VkDeviceAddress getDeviceAddress() const { return tlas.deviceAddress; }

    VkAccelerationStructureKHR getHandle() const { return tlas.accelerationStructure; }

    VkDeviceSize getBufferSize() const { return tlas.buffer.size; }

    uint32_t getInstanceCount() const { return instanceCount; }

private:
    std::vector<AccelerationStructureInstance> generateInstances(const std::vector<GaussianParticle>& particles,
                                                                 VkDeviceAddress blasDeviceAddress,
                                                                 float minAlpha,
                                                                 int   kernelDegree) const;
};

}   // namespace vk3dgrt

#endif // TLAS_H