#include "mesh-tlas.h"

#include "log.h"

#include "vulkan/vkprovider.h"
#include "vulkan/vkerror.h"


namespace vk3dgrt {

bool MeshTLAS::build(VkContext* context,
                     VkCommandBuffer cmdBuffer,
                     const std::vector<MeshTLASInstance>& meshInstances)
{
    if (built)
    {
        Log::ERR("MeshTLAS") << "MeshTLAS already built";
        return false;
    }

    if (meshInstances.empty())
    {
        Log::ERR("MeshTLAS") << "No mesh instances to build TLAS";
        return false;
    }

    builder.init(context);

    std::vector<AccelerationStructureInstance> instances = generateInstances(meshInstances);

    // Build TLAS (static mesh, no ALLOW_UPDATE needed)
    tlas = builder.buildTlas(
        cmdBuffer,
        instances,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
    );

    if (tlas.accelerationStructure == VK_NULL_HANDLE)
    {
        Log::ERR("MeshTLAS") << "Failed to build MeshTLAS";
        return false;
    }

    built = true;

    Log::OK("MeshTLAS") << "Built (" << meshInstances.size() << " instances)";

    return true;
}


bool MeshTLAS::buildAndSubmit(VkProvider* provider,
                              const std::vector<MeshTLASInstance>& meshInstances)
{
    VkContext* context = &provider->getContext();
    VkCommandBuffer cmd = provider->beginSingleTimeCommands(QueueType::GRAPHICS);

    bool result = build(context, cmd, meshInstances);

    provider->endSingleTimeCommands(cmd, QueueType::GRAPHICS);
    builder.releasePendingBuffers();

    return result;
}


void MeshTLAS::cleanup(VkContext* context)
{
    if (!built)
    {
        return;
    }

    tlas.cleanup(context);
    built = false;
}


std::vector<AccelerationStructureInstance> MeshTLAS::generateInstances(const std::vector<MeshTLASInstance>& meshInstances) const
{
    std::vector<AccelerationStructureInstance> instances;
    instances.reserve(meshInstances.size());

    for (const auto& meshInst : meshInstances)
    {
        // Convert glm::mat4 (column-major) to VkTransformMatrixKHR (3x4 row-major)
        VkTransformMatrixKHR vkTransform{};
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                vkTransform.matrix[row][col] = meshInst.transform[col][row];
            }
        }

        AccelerationStructureInstance instance{};
        instance.transform                              = vkTransform;
        instance.instanceCustomIndex                    = meshInst.meshIndex;
        instance.mask                                   = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 1;    // Hit Group 1 (mesh closest-hit)
        instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference         = meshInst.blasAddress;

        instances.push_back(instance);
    }

    return instances;
}

}   // namespace vk3dgrt
