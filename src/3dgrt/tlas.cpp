#include "tlas.h"

#include "log.h"

#include "vulkan/vkprovider.h"
#include "vulkan/vkerror.h"

namespace vk3dgrt {

bool TLAS::build(VkContext* context,
                 VkCommandBuffer cmdBuffer,
                 const GaussianParticleData& gaussianParticleData,
                 VkDeviceAddress blasDeviceAddress,
                 float minAlpha,
                 int   kernelDegree)
{
    if (built)
    {
        Log::ERR("TLAS") << "TLAS already built. Use rebuild() to update.";
        return false;
    }

    if (blasDeviceAddress == 0)
    {
        Log::ERR("TLAS") << "Invalid BLAS device address";
        return false;
    }

    if (gaussianParticleData.particles.empty())
    {
        Log::ERR("TLAS") << "No particles to build TLAS";
        return false;
    }

    // Cache BLAS address for rebuild
    cachedBlasAddress = blasDeviceAddress;
    instanceCount     = static_cast<uint32_t>(gaussianParticleData.particles.size());

    // Initialize the acceleration structure builder
    builder.init(context);

    // Generate instance array with kernel-degree-aware bounding
    // n=2 uses ~57% smaller bounding volumes → 2x fewer ray intersections
    std::vector<AccelerationStructureInstance> instances = generateInstances(
        gaussianParticleData.particles,
        blasDeviceAddress,
        minAlpha,
        kernelDegree
    );

    // Build TLAS with ALLOW_UPDATE for incremental updates
    tlas = builder.buildTlas(
        cmdBuffer,
        instances,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
    );

    if (tlas.accelerationStructure == VK_NULL_HANDLE)
    {
        Log::ERR("TLAS") << "Failed to build TLAS";
        return false;
    }

    built              = true;
    consecutiveUpdates = 0;

    Log::OK("TLAS") << "Built (" << Log::Color::Bold << Log::formatCount(instanceCount) << Log::Color::Reset << " instances)";

    return true;
}


bool TLAS::rebuild(VkContext* context,
                   VkCommandBuffer cmdBuffer,
                   const GaussianParticleData& gaussianParticleData,
                   float minAlpha,
                   int   kernelDegree)
{
    if (!built)
    {
        Log::ERR("TLAS") << "Cannot rebuild: TLAS not built yet. Call build() first.";
        return false;
    }

    if (cachedBlasAddress == 0)
    {
        Log::ERR("TLAS") << "Cannot rebuild: Invalid cached BLAS address";
        return false;
    }

    if (gaussianParticleData.particles.empty())
    {
        Log::ERR("TLAS") << "No particles to rebuild TLAS";
        return false;
    }

    uint32_t newInstanceCount = static_cast<uint32_t>(gaussianParticleData.particles.size());

    // Determine rebuild mode:
    // - Incremental update: same instance count, within consecutive update limit
    // - Full rebuild: instance count changed or too many consecutive updates
    bool shouldFullRebuild = (newInstanceCount != instanceCount) ||
                             (consecutiveUpdates >= MAX_CONSECUTIVE_UPDATES);

    // Generate new instance array
    std::vector<AccelerationStructureInstance> instances = generateInstances(
        gaussianParticleData.particles,
        cachedBlasAddress,
        minAlpha,
        kernelDegree
    );

    if (shouldFullRebuild)
    {
        tlas.cleanup(context);
        built         = false;
        instanceCount = newInstanceCount;

        tlas = builder.buildTlas(
            cmdBuffer,
            instances,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
        );

        if (tlas.accelerationStructure == VK_NULL_HANDLE)
        {
            Log::ERR("TLAS") << "Failed to rebuild TLAS";
            return false;
        }

        built              = true;
        consecutiveUpdates = 0;
    }
    else
    {
        // Incremental update: in-place update of existing TLAS
        bool updateResult = builder.updateTlas(
            cmdBuffer,
            tlas,
            instances,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
        );

        if (!updateResult)
        {
            Log::ERR("TLAS") << "Failed to update TLAS";
            return false;
        }

        consecutiveUpdates++;
    }

    return true;
}


bool TLAS::buildAndSubmit(VkProvider* provider,
                          const GaussianParticleData& gaussianParticleData,
                          VkDeviceAddress blasDeviceAddress,
                          float minAlpha,
                          int   kernelDegree)
{
    VkContext* context = &provider->getContext();
    VkCommandBuffer cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    bool result = build(context, cmd, gaussianParticleData, blasDeviceAddress, minAlpha, kernelDegree);

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);

    // Release scratch/instance buffers after GPU execution completes
    builder.releasePendingBuffers();

    return result;
}


bool TLAS::rebuildAndSubmit(VkProvider* provider,
                            const GaussianParticleData& gaussianParticleData,
                            float minAlpha,
                            int   kernelDegree)
{
    VkContext* context = &provider->getContext();
    VkCommandBuffer cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    bool result = rebuild(context, cmd, gaussianParticleData, minAlpha, kernelDegree);

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);

    // Release scratch/instance buffers after GPU execution completes
    builder.releasePendingBuffers();

    return result;
}


void TLAS::cleanup(VkContext* context)
{
    if (!built)
    {
        return;
    }

    tlas.cleanup(context);
    built              = false;
    instanceCount      = 0;
    cachedBlasAddress  = 0;
    consecutiveUpdates = 0;
}


std::vector<AccelerationStructureInstance> TLAS::generateInstances(const std::vector<GaussianParticle>& particles,
                                                                   VkDeviceAddress blasDeviceAddress,
                                                                   float minAlpha,
                                                                   int   kernelDegree) const
{
    std::vector<AccelerationStructureInstance> instances;
    instances.reserve(particles.size());

    for (size_t i = 0; i < particles.size(); ++i)
    {
        const GaussianParticle& particle = particles[i];

        // Compute transform matrix with adaptive kernel scale
        // Formula depends on kernel degree (from 3DGRT paper Section 4.2):
        //   n=1: s = sqrt(2 * log(opacity / minAlpha))       ≈ 3.03 for opacity=1
        //   n=2: s = (2 * log(opacity / minAlpha))^(1/4)     ≈ 1.74 for opacity=1
        // Transform = T * R * (S * s)
        VkTransformMatrixKHR vkTransform = computeVkInstanceTransform(particle, minAlpha, kernelDegree);

        AccelerationStructureInstance instance{};
        instance.transform                              = vkTransform;
        instance.instanceCustomIndex                    = static_cast<uint32_t>(i);  // gl_InstanceCustomIndexEXT
        instance.mask                                   = 0xFF;                       // Visible to all rays
        instance.instanceShaderBindingTableRecordOffset = 0;                          // Use first hit group
        instance.flags                                  = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;  // Force Any-Hit
        instance.accelerationStructureReference         = blasDeviceAddress;

        instances.push_back(instance);
    }

    return instances;
}

}   // namespace vk3dgrt