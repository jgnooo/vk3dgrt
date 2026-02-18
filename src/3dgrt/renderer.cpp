#include "renderer.h"

#include "log.h"

#include "vulkan/vkerror.h"

#include <array>


namespace vk3dgrt {

bool Renderer::initialize(VkContext* context,
                          uint32_t width,
                          uint32_t height,
                          const std::string& shaderPath)
{
    if (initialized)
    {
        Log::ERR("Render") << "Already initialized";
        return false;
    }

    ctx = context;

    // 1. Initialize RT descriptor set
    if (!rtDescriptorSet.initialize(ctx))
    {
        Log::ERR("Render") << "Failed to initialize RT descriptor set";
        return false;
    }

    // 2. Create uniform buffers
    if (!createUniformBuffers())
    {
        Log::ERR("Render") << "Failed to create uniform buffers";
        return false;
    }

    // 3. Create output image
    if (!createOutputImage(width, height))
    {
        Log::ERR("Render") << "Failed to create output image";
        return false;
    }

    // 3.5. Create accumulation resources (for DoF)
    if (!createAccumulationResources(width, height, shaderPath))
    {
        Log::ERR("Render") << "Failed to create accumulation resources";
        return false;
    }

    // 4. Load shaders
    if (!loadShaders(shaderPath))
    {
        Log::ERR("Render") << "Failed to load shaders";
        return false;
    }

    // 5. Create ray tracing pipeline
    if (!createPipeline())
    {
        Log::ERR("Render") << "Failed to create pipeline";
        return false;
    }

    // 6. Create shader binding table
    if (!createShaderBindingTable())
    {
        Log::ERR("Render") << "Failed to create shader binding table";
        return false;
    }

    initialized = true;

    Log::OK("Render") << "Pipeline ready (" << Log::Color::Bold << width << "x" << height << Log::Color::Reset << ")";

    return true;
}


void Renderer::cleanup(VkContext* context)
{
    if (!initialized)
    {
        return;
    }

    VkDevice device = context->getDevice();

    // Wait for device to be idle
    vkDeviceWaitIdle(device);

    // Cleanup shader binding table
    sbt.cleanup(context);

    // Cleanup pipeline
    if (pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    // Cleanup pipeline builder
    pipelineBuilder.cleanup();

    // Cleanup shaders
    raygenShader.cleanup(device);
    missShader.cleanup(device);
    closestHitShader.cleanup(device);
    anyHitShader.cleanup(device);
    meshClosestHitShader.cleanup(device);

    // Cleanup accumulation resources
    cleanupAccumulationResources();

    // Cleanup output image
    outputImage.cleanup(context);

    // Cleanup uniform buffers (camera now uses push constants)
    sceneBoundsUBO.cleanup(context->getAllocator());

    // Cleanup RT descriptor set
    rtDescriptorSet.cleanup(device);

    initialized = false;
    ctx         = nullptr;

    // Reset push constants
    cameraPushConstants  = CameraPushConstants{};
}


bool Renderer::updateDescriptors(const TLAS& tlas,
                                 const GaussianParticleBuffers& gaussianParticleBuffers,
                                 const MeshTLAS* meshTlas,
                                 const MeshBuffers* meshBuffers)
{
    if (!initialized)
    {
        Log::ERR("Render") << "Cannot update descriptors: not initialized";
        return false;
    }

    if (!tlas.isBuilt())
    {
        Log::ERR("Render") << "Cannot update descriptors: TLAS not built";
        return false;
    }

    if (!gaussianParticleBuffers.isInitialized())
    {
        Log::ERR("Render") << "Cannot update descriptors: GaussianParticleBuffers not initialized";
        return false;
    }

    // Store SoA buffer device addresses for push constants (bindless access)
    cameraPushConstants.positionBufferAddress   = gaussianParticleBuffers.getPositionBufferAddress();
    cameraPushConstants.colorBufferAddress      = gaussianParticleBuffers.getColorBufferAddress();
    cameraPushConstants.quaternionBufferAddress = gaussianParticleBuffers.getQuaternionBufferAddress();
    cameraPushConstants.scaleBufferAddress      = gaussianParticleBuffers.getScaleBufferAddress();
    cameraPushConstants.shBufferAddress         = gaussianParticleBuffers.hasSHCoefficients()
                                                      ? gaussianParticleBuffers.getSHBufferAddress()
                                                      : 0;

    // Prepare mesh descriptor parameters (VK_NULL_HANDLE if no mesh)
    VkAccelerationStructureKHR meshTlasHandle  = VK_NULL_HANDLE;
    VkBuffer meshVertexBuffer                  = VK_NULL_HANDLE;
    VkDeviceSize meshVertexSize                = 0;
    VkBuffer meshNormalBuffer                  = VK_NULL_HANDLE;
    VkDeviceSize meshNormalSize                = 0;
    VkBuffer meshIndexBuffer                   = VK_NULL_HANDLE;
    VkDeviceSize meshIndexSize                 = 0;
    VkBuffer meshMaterialBuffer                = VK_NULL_HANDLE;
    VkDeviceSize meshMaterialSize              = 0;

    if (meshTlas != nullptr && meshTlas->isBuilt() &&
        meshBuffers != nullptr && meshBuffers->isInitialized())
    {
        meshTlasHandle     = meshTlas->getHandle();
        meshVertexBuffer   = meshBuffers->getVertexBufferHandle();
        meshVertexSize     = meshBuffers->getVertexBufferSize();
        meshNormalBuffer   = meshBuffers->getNormalBufferHandle();
        meshNormalSize     = meshBuffers->getNormalBufferSize();
        meshIndexBuffer    = meshBuffers->getIndexBufferHandle();
        meshIndexSize      = meshBuffers->getIndexBufferSize();
        meshMaterialBuffer = meshBuffers->getMaterialBufferHandle();
        meshMaterialSize   = meshBuffers->getMaterialBufferSize();
    }

    return rtDescriptorSet.update(
        tlas.getHandle(),
        outputImage.imageView,
        sceneBoundsUBO.buffer,
        sizeof(SceneBoundsUBO),
        meshTlasHandle,
        meshVertexBuffer,
        meshVertexSize,
        meshNormalBuffer,
        meshNormalSize,
        meshIndexBuffer,
        meshIndexSize,
        meshMaterialBuffer,
        meshMaterialSize
    );
}


void Renderer::updateCamera(const CameraPushConstants& camera)
{
    if (!initialized)
    {
        return;
    }

    // Store camera data for push constants (will be pushed during recordRayTrace)
    // Preserve BDA fields that were set by updateDescriptors()
    VkDeviceAddress savedPositionAddr   = cameraPushConstants.positionBufferAddress;
    VkDeviceAddress savedColorAddr      = cameraPushConstants.colorBufferAddress;
    VkDeviceAddress savedQuaternionAddr = cameraPushConstants.quaternionBufferAddress;
    VkDeviceAddress savedScaleAddr      = cameraPushConstants.scaleBufferAddress;
    VkDeviceAddress savedSHAddr         = cameraPushConstants.shBufferAddress;

    cameraPushConstants = camera;

    cameraPushConstants.positionBufferAddress   = savedPositionAddr;
    cameraPushConstants.colorBufferAddress      = savedColorAddr;
    cameraPushConstants.quaternionBufferAddress = savedQuaternionAddr;
    cameraPushConstants.scaleBufferAddress      = savedScaleAddr;
    cameraPushConstants.shBufferAddress         = savedSHAddr;
}


void Renderer::updateSceneBounds(const SceneBoundsUBO& bounds)
{
    if (!initialized)
    {
        return;
    }

    sceneBoundsUBO.upload(&bounds, sizeof(SceneBoundsUBO), 0);
}


void Renderer::recordRayTrace(VkCommandBuffer cmdBuffer)
{
    if (!initialized)
    {
        return;
    }

    // Transition output image to GENERAL layout for storage image access
    outputImage.transitionLayout(
        cmdBuffer,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0,
        VK_ACCESS_SHADER_WRITE_BIT
    );

    // Bind ray tracing pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);

    // Bind descriptor set
    rtDescriptorSet.bind(cmdBuffer, pipelineLayout);

    // Push camera constants (faster than UBO update per frame)
    vkCmdPushConstants(
        cmdBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        0,
        sizeof(CameraPushConstants),
        &cameraPushConstants
    );

    // Dispatch rays
    // Empty callable region (not used)
    VkStridedDeviceAddressRegionKHR emptyCallableRegion{};

    vkCmdTraceRaysKHR_(
        cmdBuffer,
        &sbt.raygenRegion,
        &sbt.missRegion,
        &sbt.hitRegion,
        &emptyCallableRegion,
        outputImage.extent.width,
        outputImage.extent.height,
        1  // depth
    );
}


void Renderer::copyToSwapchain(VkCommandBuffer cmdBuffer,
                               VkImage dstImage,
                               VkExtent2D dstExtent)
{
    if (!initialized)
    {
        return;
    }

    // Choose source image: accumImage (DoF active) or outputImage (normal)
    bool useAccum = isDoFActive() && accumImage.image != VK_NULL_HANDLE;
    AllocatedImage& srcImage = useAccum ? accumImage : outputImage;

    // Transition source image to TRANSFER_SRC layout
    VkPipelineStageFlags srcStage = useAccum
        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    VkAccessFlags srcAccess = VK_ACCESS_SHADER_WRITE_BIT;

    srcImage.transitionLayout(
        cmdBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        srcStage,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        srcAccess,
        VK_ACCESS_TRANSFER_READ_BIT
    );

    // Transition destination image to TRANSFER_DST layout
    VkImageMemoryBarrier dstBarrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dstImage,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &dstBarrier
    );

    // Blit from source image to swapchain image
    VkImageBlit blitRegion{
        .srcSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1
        },
        .srcOffsets = {
            {0, 0, 0},
            {static_cast<int32_t>(srcImage.extent.width),
             static_cast<int32_t>(srcImage.extent.height),
             1}
        },
        .dstSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1
        },
        .dstOffsets = {
            {0, 0, 0},
            {static_cast<int32_t>(dstExtent.width),
             static_cast<int32_t>(dstExtent.height),
             1}
        }
    };

    vkCmdBlitImage(
        cmdBuffer,
        srcImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        VK_FILTER_LINEAR
    );

    // Transition destination image to PRESENT_SRC layout
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = 0;
    dstBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dstBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &dstBarrier
    );
}


bool Renderer::resize(uint32_t width, uint32_t height)
{
    if (!initialized)
    {
        return false;
    }

    if (width == 0 || height == 0)
    {
        return false;
    }

    // Wait for device to be idle before recreating resources
    vkDeviceWaitIdle(ctx->getDevice());

    // Cleanup old output image
    outputImage.cleanup(ctx);

    // Cleanup old accumulation image
    accumImage.cleanup(ctx);

    // Create new output image
    if (!createOutputImage(width, height))
    {
        Log::ERR("Render") << "Failed to resize output image";
        return false;
    }

    // Create new accumulation image
    accumImage.create(
        ctx, width, height,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        ImageUsage::STORAGE
    );

    // Update descriptor for output image (only binding 1 changed)
    rtDescriptorSet.updateOutputImage(outputImage.imageView);

    // Update accumulation descriptors
    updateAccumDescriptors();

    return true;
}


bool Renderer::createUniformBuffers()
{
    // Initialize camera push constants with defaults
    cameraPushConstants.viewInverse  = glm::mat4(1.0f);
    cameraPushConstants.projInverse  = glm::mat4(1.0f);
    cameraPushConstants.nearPlane    = 0.1f;
    cameraPushConstants.farPlane     = 1000.0f;

    // Scene Bounds UBO (still uses uniform buffer - less frequently updated)
    sceneBoundsUBO.create(
        ctx,
        sizeof(SceneBoundsUBO),
        BufferUsage::UNIFORM,
        true  // Host visible
    );

    // Initialize with default bounds
    SceneBoundsUBO initialBounds{};
    initialBounds.minBound = glm::vec3(-100.0f);
    initialBounds.tMin     = 0.001f;
    initialBounds.maxBound = glm::vec3(100.0f);
    initialBounds.tMax     = 10000.0f;
    sceneBoundsUBO.upload(&initialBounds, sizeof(SceneBoundsUBO), 0);

    return true;
}


bool Renderer::createOutputImage(uint32_t width, uint32_t height)
{
    outputImage.create(
        ctx,
        width,
        height,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        ImageUsage::STORAGE
    );

    if (outputImage.image == VK_NULL_HANDLE)
    {
        Log::ERR("Render") << "Failed to create output image";
        return false;
    }

    return true;
}


bool Renderer::loadShaders(const std::string& shaderPath)
{
    try
    {
        // Shader file names (compiled SPIR-V)
        std::string raygenFile   = shaderPath + "ray-generation.rgen.spv";
        std::string missFile     = shaderPath + "miss.rmiss.spv";
        std::string chitFile     = shaderPath + "closest-hit.rchit.spv";
        std::string ahitFile     = shaderPath + "any-hit.rahit.spv";
        std::string meshChitFile = shaderPath + "mesh-closest-hit.rchit.spv";

        raygenShader.createFromFile(ctx->getDevice(), raygenFile);
        missShader.createFromFile(ctx->getDevice(), missFile);
        closestHitShader.createFromFile(ctx->getDevice(), chitFile);
        anyHitShader.createFromFile(ctx->getDevice(), ahitFile);
        meshClosestHitShader.createFromFile(ctx->getDevice(), meshChitFile);
    }
    catch (const std::exception& e)
    {
        Log::ERR("Render") << "Shader loading failed: " << e.what();
        return false;
    }

    return true;
}


bool Renderer::createPipeline()
{
    VkDevice device = ctx->getDevice();

    // Push constant range for camera data
    VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        .offset     = 0,
        .size       = sizeof(CameraPushConstants)
    };

    // Create pipeline layout with push constants
    VkDescriptorSetLayout rtLayout = rtDescriptorSet.getLayout();
    VkPipelineLayoutCreateInfo layoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &rtLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstantRange
    };

    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));

    // Initialize pipeline builder
    pipelineBuilder.init(ctx);

    // Add shader stages
    uint32_t raygenIdx   = pipelineBuilder.addRaygenShader(raygenShader.module);
    uint32_t missIdx     = pipelineBuilder.addMissShader(missShader.module);
    uint32_t chitIdx     = pipelineBuilder.addClosestHitShader(closestHitShader.module);
    uint32_t ahitIdx     = pipelineBuilder.addAnyHitShader(anyHitShader.module);
    uint32_t meshChitIdx = pipelineBuilder.addClosestHitShader(meshClosestHitShader.module);

    // Add shader groups
    // Group 0: RayGen
    pipelineBuilder.addRaygenGroup(raygenIdx);

    // Group 1: Miss
    pipelineBuilder.addMissGroup(missIdx);

    // Group 2: Hit Group 0 — Gaussian (any-hit for sorting, closest-hit skipped via ray flags)
    pipelineBuilder.addHitGroup(chitIdx, ahitIdx, VK_SHADER_UNUSED_KHR);

    // Group 3: Hit Group 1 — Mesh (closest-hit only, no any-hit needed)
    pipelineBuilder.addHitGroup(meshChitIdx, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR);

    // Build pipeline
    // maxRecursionDepth = 1: raygen(depth 0) → hit/miss(depth 1), hit shaders don't trace recursively
    pipeline = pipelineBuilder.build(pipelineLayout, 1);

    if (pipeline == VK_NULL_HANDLE)
    {
        Log::ERR("Render") << "Failed to create ray tracing pipeline";
        return false;
    }

    return true;
}


bool Renderer::createShaderBindingTable()
{
    sbt.create(ctx, pipeline, pipelineBuilder);

    return true;
}


void Renderer::recordAccumulation(VkCommandBuffer cmdBuffer, uint32_t frameIndex)
{
    if (!initialized || accumPipeline == VK_NULL_HANDLE)
    {
        return;
    }

    // outputImage is already in GENERAL layout from recordRayTrace
    // Transition accumImage to GENERAL for compute shader access
    accumImage.transitionLayout(
        cmdBuffer,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
    );

    // Memory barrier: ensure RT writes to outputImage are visible to compute reads
    VkMemoryBarrier memBarrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1, &memBarrier,
        0, nullptr,
        0, nullptr
    );

    // Bind compute pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumPipeline);

    // Bind accumulation descriptor set
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        accumPipeLayout,
        0, 1, &accumDescSet,
        0, nullptr
    );

    // Push frame index
    vkCmdPushConstants(
        cmdBuffer,
        accumPipeLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(uint32_t),
        &frameIndex
    );

    // Dispatch compute shader (16x16 workgroups)
    uint32_t groupCountX = (outputImage.extent.width  + 15) / 16;
    uint32_t groupCountY = (outputImage.extent.height + 15) / 16;
    vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);
}


bool Renderer::createAccumulationResources(uint32_t width, uint32_t height, const std::string& shaderPath)
{
    VkDevice device = ctx->getDevice();

    // 1. Create accumulation image (rgba32f for high-precision accumulation)
    accumImage.create(
        ctx, width, height,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        ImageUsage::STORAGE
    );

    if (accumImage.image == VK_NULL_HANDLE)
    {
        Log::ERR("Render") << "Failed to create accumulation image";
        return false;
    }

    // 2. Load accumulation compute shader
    try
    {
        std::string accumFile = shaderPath + "accumulate.comp.spv";
        accumShader.createFromFile(device, accumFile);
    }
    catch (const std::exception& e)
    {
        Log::ERR("Render") << "Accumulation shader loading failed: " << e.what();
        return false;
    }

    // 3. Create descriptor set layout (2 storage images)
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {{
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT
        }
    }};

    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings    = bindings.data()
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &accumDescSetLayout));

    // 4. Create descriptor pool
    VkDescriptorPoolSize poolSize{
        .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 2
    };

    VkDescriptorPoolCreateInfo poolInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = 1,
        .pPoolSizes    = &poolSize
    };

    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &accumDescPool));

    // 5. Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = accumDescPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &accumDescSetLayout
    };

    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &accumDescSet));

    // 6. Write descriptors
    updateAccumDescriptors();

    // 7. Create pipeline layout with push constant (uint frameIndex)
    VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset     = 0,
        .size       = sizeof(uint32_t)
    };

    VkPipelineLayoutCreateInfo pipeLayoutInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &accumDescSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushRange
    };

    VK_CHECK(vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &accumPipeLayout));

    // 8. Create compute pipeline
    VkComputePipelineCreateInfo computeInfo{
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = accumShader.module,
            .pName  = "main"
        },
        .layout = accumPipeLayout
    };

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &accumPipeline));

    Log::OK("Render") << "Accumulation pipeline created";

    return true;
}


void Renderer::cleanupAccumulationResources()
{
    if (!ctx)
    {
        return;
    }

    VkDevice device = ctx->getDevice();

    if (accumPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, accumPipeline, nullptr);
        accumPipeline = VK_NULL_HANDLE;
    }

    if (accumPipeLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, accumPipeLayout, nullptr);
        accumPipeLayout = VK_NULL_HANDLE;
    }

    if (accumDescPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, accumDescPool, nullptr);
        accumDescPool = VK_NULL_HANDLE;
        accumDescSet  = VK_NULL_HANDLE;  // Implicitly freed with pool
    }

    if (accumDescSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, accumDescSetLayout, nullptr);
        accumDescSetLayout = VK_NULL_HANDLE;
    }

    accumShader.cleanup(device);
    accumImage.cleanup(ctx);
}


void Renderer::updateAccumDescriptors()
{
    VkDescriptorImageInfo outputInfo{
        .imageView   = outputImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    VkDescriptorImageInfo accumInfo{
        .imageView   = accumImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    std::array<VkWriteDescriptorSet, 2> writes = {{
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = accumDescSet,
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = &outputInfo
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = accumDescSet,
            .dstBinding      = 1,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = &accumInfo
        }
    }};

    vkUpdateDescriptorSets(ctx->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}


}   // namespace vk3dgrt