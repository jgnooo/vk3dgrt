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

    // Transition output image to TRANSFER_SRC layout
    outputImage.transitionLayout(
        cmdBuffer,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
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

    // Blit from output image to swapchain image
    VkImageBlit blitRegion{
        .srcSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1
        },
        .srcOffsets = {
            {0, 0, 0},
            {static_cast<int32_t>(outputImage.extent.width),
             static_cast<int32_t>(outputImage.extent.height),
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
        outputImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        VK_FILTER_LINEAR  // Linear filtering for any scaling
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

    // Create new output image
    if (!createOutputImage(width, height))
    {
        Log::ERR("Render") << "Failed to resize output image";
        return false;
    }

    // Update descriptor for output image (only binding 1 changed)
    rtDescriptorSet.updateOutputImage(outputImage.imageView);

    return true;
}


bool Renderer::createUniformBuffers()
{
    // Initialize camera push constants with defaults
    cameraPushConstants.viewInverse  = glm::mat4(1.0f);
    cameraPushConstants.projInverse  = glm::mat4(1.0f);
    cameraPushConstants.position     = glm::vec3(0.0f, 0.0f, 5.0f);
    cameraPushConstants.fov          = 60.0f;
    cameraPushConstants.forward      = glm::vec3(0.0f, 0.0f, -1.0f);
    cameraPushConstants.aspectRatio  = 16.0f / 9.0f;
    cameraPushConstants.right        = glm::vec3(1.0f, 0.0f, 0.0f);
    cameraPushConstants.nearPlane    = 0.1f;
    cameraPushConstants.up           = glm::vec3(0.0f, 1.0f, 0.0f);
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

}   // namespace vk3dgrt