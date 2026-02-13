#include "vkpipeline.h"
#include "vkcontext.h"
#include "vkerror.h"

#include <stdexcept>
#include <cstdio>


// --------------------------------------------------- //
//  DescriptorSetLayout Implementation
// --------------------------------------------------- //

void DescriptorSetLayout::create(VkDevice device,
                                 const std::vector<DescriptorSetLayoutBinding>& bindings)
{
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(bindings.size());

    for (const auto& binding : bindings)
    {
        VkDescriptorSetLayoutBinding vkBinding{
            .binding            = binding.binding,
            .descriptorType     = binding.type,
            .descriptorCount    = binding.count,
            .stageFlags         = binding.stageFlags,
            .pImmutableSamplers = nullptr
        };
        vkBindings.push_back(vkBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(vkBindings.size()),
        .pBindings    = vkBindings.data()
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout));
}


void DescriptorSetLayout::cleanup(VkDevice device)
{
    if (layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
}


// --------------------------------------------------- //
//  DescriptorAllocator Implementation
// --------------------------------------------------- //

void DescriptorAllocator::create(VkDevice device,
                                 uint32_t maxSets,
                                 const std::vector<VkDescriptorPoolSize>& poolSizes)
{
    VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = maxSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes    = poolSizes.data()
    };

    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
}


void DescriptorAllocator::cleanup(VkDevice device)
{
    if (pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
}


VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &layout
    };

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &set));
    return set;
}


// --------------------------------------------------- //
//  ShaderModule Implementation
// --------------------------------------------------- //

void ShaderModule::create(VkDevice device, const std::vector<uint32_t>& spirvCode)
{
    VkShaderModuleCreateInfo createInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvCode.size() * sizeof(uint32_t),
        .pCode    = spirvCode.data()
    };

    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &module));
}


void ShaderModule::createFromFile(VkDevice device, const std::string& filepath)
{
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file)
    {
        throw std::runtime_error("[ShaderModule] Failed to open shader file: " + filepath);
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    std::vector<uint32_t> spirvCode(fileSize / sizeof(uint32_t));
    fread(spirvCode.data(), sizeof(uint32_t), spirvCode.size(), file);
    fclose(file);

    create(device, spirvCode);
}


void ShaderModule::cleanup(VkDevice device)
{
    if (module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, module, nullptr);
        module = VK_NULL_HANDLE;
    }
}


// --------------------------------------------------- //
//  RayTracingPipelineBuilder Implementation
// --------------------------------------------------- //

void RayTracingPipelineBuilder::init(VkContext* ctx)
{
    context = ctx;
    shaderStages.clear();
    shaderModules.clear();
    shaderGroups.clear();
}


void RayTracingPipelineBuilder::cleanup()
{
    shaderStages.clear();
    shaderModules.clear();
    shaderGroups.clear();

    specInfo_ = nullptr;
}


uint32_t RayTracingPipelineBuilder::addShaderStage(VkShaderModule module, VkShaderStageFlagBits stage)
{
    uint32_t index = static_cast<uint32_t>(shaderStages.size());

    VkPipelineShaderStageCreateInfo stageInfo{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = stage,
        .module = module,
        .pName  = "main",
        .pSpecializationInfo = specInfo_
    };

    shaderStages.push_back(stageInfo);
    shaderModules.push_back(module);

    return index;
}


uint32_t RayTracingPipelineBuilder::addRaygenShader(VkShaderModule module)
{
    return addShaderStage(module, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
}


uint32_t RayTracingPipelineBuilder::addMissShader(VkShaderModule module)
{
    return addShaderStage(module, VK_SHADER_STAGE_MISS_BIT_KHR);
}


uint32_t RayTracingPipelineBuilder::addClosestHitShader(VkShaderModule module)
{
    return addShaderStage(module, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
}


uint32_t RayTracingPipelineBuilder::addAnyHitShader(VkShaderModule module)
{
    return addShaderStage(module, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
}


void RayTracingPipelineBuilder::addRaygenGroup(uint32_t raygenShaderIndex)
{
    RayTracingShaderGroup group{
        .type          = ShaderGroupType::RAYGEN,
        .generalShader = raygenShaderIndex
    };
    shaderGroups.push_back(group);
}


void RayTracingPipelineBuilder::addMissGroup(uint32_t missShaderIndex)
{
    RayTracingShaderGroup group{
        .type          = ShaderGroupType::MISS,
        .generalShader = missShaderIndex
    };
    shaderGroups.push_back(group);
}


void RayTracingPipelineBuilder::addHitGroup(uint32_t closestHitShaderIndex,
                                            uint32_t anyHitShaderIndex,
                                            uint32_t intersectionShaderIndex)
{
    RayTracingShaderGroup group{
        .type               = ShaderGroupType::HIT,
        .closestHitShader   = closestHitShaderIndex,
        .anyHitShader       = anyHitShaderIndex,
        .intersectionShader = intersectionShaderIndex
    };
    shaderGroups.push_back(group);
}


void RayTracingPipelineBuilder::setSpecializationInfo(const VkSpecializationInfo* specInfo)
{
    specInfo_ = specInfo;
}


VkPipeline RayTracingPipelineBuilder::build(VkPipelineLayout layout, uint32_t maxRecursionDepth)
{
    // Convert shader groups to Vulkan format
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> vkGroups;
    vkGroups.reserve(shaderGroups.size());

    for (const auto& group : shaderGroups)
    {
        VkRayTracingShaderGroupCreateInfoKHR vkGroup{
            .sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .generalShader      = VK_SHADER_UNUSED_KHR,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        };

        switch (group.type)
        {
            case ShaderGroupType::RAYGEN:
            case ShaderGroupType::MISS:
            case ShaderGroupType::CALLABLE:
                vkGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                vkGroup.generalShader = group.generalShader;
                break;

            case ShaderGroupType::HIT:
                if (group.intersectionShader != VK_SHADER_UNUSED_KHR)
                {
                    vkGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
                }
                else
                {
                    vkGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                }
                vkGroup.closestHitShader   = group.closestHitShader;
                vkGroup.anyHitShader       = group.anyHitShader;
                vkGroup.intersectionShader = group.intersectionShader;
                break;
        }

        vkGroups.push_back(vkGroup);
    }

    VkRayTracingPipelineCreateInfoKHR pipelineInfo{
        .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount                   = static_cast<uint32_t>(shaderStages.size()),
        .pStages                      = shaderStages.data(),
        .groupCount                   = static_cast<uint32_t>(vkGroups.size()),
        .pGroups                      = vkGroups.data(),
        .maxPipelineRayRecursionDepth = maxRecursionDepth,
        .layout                       = layout
    };

    VkPipeline pipeline;
    VK_CHECK(vkCreateRayTracingPipelinesKHR_(
        context->getDevice(),
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline
    ));

    return pipeline;
}


// --------------------------------------------------- //
//  ShaderBindingTable Implementation
// --------------------------------------------------- //

uint32_t ShaderBindingTable::alignUp(uint32_t size, uint32_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}


void ShaderBindingTable::create(VkContext* context,
                                VkPipeline pipeline,
                                const RayTracingPipelineBuilder& pipelineBuilder)
{
    const uint32_t handleSize        = context->rtProps.shaderGroupHandleSize;
    const uint32_t handleAlignment   = context->rtProps.shaderGroupHandleAlignment;
    const uint32_t baseAlignment     = context->rtProps.shaderGroupBaseAlignment;
    const uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);

    // Count groups by type
    uint32_t raygenCount   = 0;
    uint32_t missCount     = 0;
    uint32_t hitCount      = 0;
    uint32_t callableCount = 0;

    for (const auto& group : pipelineBuilder.shaderGroups)
    {
        switch (group.type)
        {
            case ShaderGroupType::RAYGEN:   raygenCount++;   break;
            case ShaderGroupType::MISS:     missCount++;     break;
            case ShaderGroupType::HIT:      hitCount++;      break;
            case ShaderGroupType::CALLABLE: callableCount++; break;
        }
    }

    // Get all shader group handles
    uint32_t groupCount = static_cast<uint32_t>(pipelineBuilder.shaderGroups.size());
    uint32_t dataSize   = groupCount * handleSize;
    std::vector<uint8_t> handles(dataSize);

    VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR_(
        context->getDevice(),
        pipeline,
        0,
        groupCount,
        dataSize,
        handles.data()
    ));

    // Calculate sizes
    VkDeviceSize raygenSize   = alignUp(raygenCount * handleSizeAligned, baseAlignment);
    VkDeviceSize missSize     = alignUp(missCount * handleSizeAligned, baseAlignment);
    VkDeviceSize hitSize      = alignUp(hitCount * handleSizeAligned, baseAlignment);
    VkDeviceSize callableSize = alignUp(callableCount * handleSizeAligned, baseAlignment);

    // Create SBT buffers
    if (raygenCount > 0)
    {
        raygenSbt.create(context, raygenSize, BufferUsage::SHADER_BINDING_TABLE, true);
        // NOTE: For raygen, size must equal stride (only one entry allowed per Vulkan spec)
        raygenRegion = {
            .deviceAddress = raygenSbt.deviceAddress,
            .stride        = handleSizeAligned,
            .size          = handleSizeAligned
        };
    }

    if (missCount > 0)
    {
        missSbt.create(context, missSize, BufferUsage::SHADER_BINDING_TABLE, true);
        missRegion = {
            .deviceAddress = missSbt.deviceAddress,
            .stride        = handleSizeAligned,
            .size          = missSize
        };
    }

    if (hitCount > 0)
    {
        hitSbt.create(context, hitSize, BufferUsage::SHADER_BINDING_TABLE, true);
        hitRegion = {
            .deviceAddress = hitSbt.deviceAddress,
            .stride        = handleSizeAligned,
            .size          = hitSize
        };
    }

    if (callableCount > 0)
    {
        callableSbt.create(context, callableSize, BufferUsage::SHADER_BINDING_TABLE, true);
        callableRegion = {
            .deviceAddress = callableSbt.deviceAddress,
            .stride        = handleSizeAligned,
            .size          = callableSize
        };
    }

    // Copy handles to SBT buffers
    uint32_t raygenOffset   = 0;
    uint32_t missOffset     = 0;
    uint32_t hitOffset      = 0;
    uint32_t callableOffset = 0;

    for (uint32_t i = 0; i < groupCount; ++i)
    {
        const auto& group  = pipelineBuilder.shaderGroups[i];
        uint8_t* handleData = handles.data() + i * handleSize;

        switch (group.type)
        {
            case ShaderGroupType::RAYGEN:
                raygenSbt.upload(handleData, handleSize, raygenOffset * handleSizeAligned);
                raygenOffset++;
                break;

            case ShaderGroupType::MISS:
                missSbt.upload(handleData, handleSize, missOffset * handleSizeAligned);
                missOffset++;
                break;

            case ShaderGroupType::HIT:
                hitSbt.upload(handleData, handleSize, hitOffset * handleSizeAligned);
                hitOffset++;
                break;

            case ShaderGroupType::CALLABLE:
                callableSbt.upload(handleData, handleSize, callableOffset * handleSizeAligned);
                callableOffset++;
                break;
        }
    }
}


void ShaderBindingTable::cleanup(VkContext* context)
{
    VmaAllocator allocator = context->getAllocator();

    raygenSbt.cleanup(allocator);
    missSbt.cleanup(allocator);
    hitSbt.cleanup(allocator);
    callableSbt.cleanup(allocator);

    raygenRegion   = {};
    missRegion     = {};
    hitRegion      = {};
    callableRegion = {};
}