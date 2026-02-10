#ifndef VKPIPELINE_H
#define VKPIPELINE_H

#include "vkbuffer.h"

#include <vulkan/vulkan_core.h>

#include <vector>
#include <string>
#include <cstdint>


struct VkContext;


struct DescriptorSetLayoutBinding
{
    uint32_t           binding;
    VkDescriptorType   type;
    uint32_t           count;
    VkShaderStageFlags stageFlags;
};


struct DescriptorSetLayout
{
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;

    void create(VkDevice device, const std::vector<DescriptorSetLayoutBinding>& bindings);
    void cleanup(VkDevice device);
};


struct DescriptorAllocator
{
    VkDescriptorPool pool = VK_NULL_HANDLE;

    void create(VkDevice device, uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& poolSizes);
    void cleanup(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};


struct ShaderModule
{
    VkShaderModule module = VK_NULL_HANDLE;

    void create(VkDevice device, const std::vector<uint32_t>& spirvCode);
    void createFromFile(VkDevice device, const std::string& filepath);
    void cleanup(VkDevice device);
};


enum class ShaderGroupType
{
    RAYGEN,
    MISS,
    HIT,
    CALLABLE
};


struct RayTracingShaderGroup
{
    ShaderGroupType type;
    uint32_t        generalShader      = VK_SHADER_UNUSED_KHR;
    uint32_t        closestHitShader   = VK_SHADER_UNUSED_KHR;
    uint32_t        anyHitShader       = VK_SHADER_UNUSED_KHR;
    uint32_t        intersectionShader = VK_SHADER_UNUSED_KHR;
};


struct RayTracingPipelineBuilder
{
    VkContext* context = nullptr;

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<VkShaderModule>                  shaderModules;
    std::vector<RayTracingShaderGroup>           shaderGroups;

    void init(VkContext* ctx);
    void cleanup();

    // Add shader stages
    uint32_t addRaygenShader(VkShaderModule module);
    uint32_t addMissShader(VkShaderModule module);
    uint32_t addClosestHitShader(VkShaderModule module);
    uint32_t addAnyHitShader(VkShaderModule module);

    // Add shader groups
    void addRaygenGroup(uint32_t raygenShaderIndex);
    void addMissGroup(uint32_t missShaderIndex);
    void addHitGroup(uint32_t closestHitShaderIndex   = VK_SHADER_UNUSED_KHR,
                     uint32_t anyHitShaderIndex       = VK_SHADER_UNUSED_KHR,
                     uint32_t intersectionShaderIndex = VK_SHADER_UNUSED_KHR);
    
    // Build the pipeline
    VkPipeline build(VkPipelineLayout layout, uint32_t maxRecursionDepth = 1);

private:
    uint32_t addShaderStage(VkShaderModule module, VkShaderStageFlagBits stage);
};


struct ShaderBindingTable
{
    AllocatedBuffer raygenSbt;
    AllocatedBuffer missSbt;
    AllocatedBuffer hitSbt;
    AllocatedBuffer callableSbt;

    VkStridedDeviceAddressRegionKHR raygenRegion   = {};
    VkStridedDeviceAddressRegionKHR missRegion     = {};
    VkStridedDeviceAddressRegionKHR hitRegion      = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};

    void create(VkContext* context,
                VkPipeline pipeline,
                const RayTracingPipelineBuilder& pipelineBuilder);
    void cleanup(VkContext* context);

private:
    uint32_t alignUp(uint32_t size, uint32_t alignment);
};

#endif // VKPIPELINE_H