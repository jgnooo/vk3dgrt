#include "grt-scene.h"

#include "log.h"

#include "vulkan/vkprovider.h"
#include "vulkan/vkerror.h"

#include <GLFW/glfw3.h>

#include <algorithm>


namespace vk3dgrt {

bool GRTScene::initialize(VkProvider* provider)
{
    if (initialized)
    {
        Log::ERR("Scene") << "Already initialized";
        return false;
    }

    provider_ = provider;
    return true;
}


void GRTScene::cleanup()
{
    if (!initialized && !provider_)
    {
        return;
    }

    if (provider_)
    {
        VkContext* context = &provider_->getContext();

        // Wait for device to be idle
        vkDeviceWaitIdle(context->getDevice());

        // Shutdown camera controller
        cameraController.shutdown();

        // Cleanup renderer
        renderer.cleanup(context);

        // Cleanup acceleration structures
        tlas.cleanup(context);
        blas.cleanup(context);

        // Cleanup mesh resources
        meshTlas_.cleanup(context);
        meshBlas_.cleanup(context);
        meshBuffers_.cleanup(context->getAllocator());
        meshInstances_.clear();

        // Cleanup GPU buffers
        gaussianParticleBuffers.cleanup(context->getAllocator());
    }

    // Clear CPU data
    gaussianParticleData.clear();

    initialized = false;
    dataLoaded  = false;
    provider_   = nullptr;
}


void GRTScene::recordCommands(VkCommandBuffer cmdBuffer)
{
    if (!initialized || !dataLoaded)
    {
        return;
    }

    // In-place TLAS update for mesh transform changes (no vkDeviceWaitIdle)
    if (meshTlasDirty_ && meshBlas_.isBuilt())
    {
        auto meshTLASInstances = buildMeshTLASInstances();
        meshTlas_.recordUpdate(cmdBuffer, meshFrameIndex_, meshTLASInstances);
        meshTlasDirty_ = false;
    }

    // Advance frame index for double-buffered resources
    meshFrameIndex_ = 1 - meshFrameIndex_;

    renderer.recordRayTrace(cmdBuffer);

    // DoF accumulation: blend current frame into accumulation buffer
    if (dofEnabled_)
    {
        renderer.recordAccumulation(cmdBuffer, frameIndex_);
    }
}


void GRTScene::copyToSwapchain(VkCommandBuffer cmdBuffer,
                               VkImage dstImage,
                               VkExtent2D dstExtent)
{
    if (!initialized || !dataLoaded)
    {
        return;
    }

    renderer.copyToSwapchain(cmdBuffer, dstImage, dstExtent);
}


void GRTScene::update(float deltaTime)
{
    if (!initialized)
    {
        return;
    }

    // Store previous camera state for movement detection
    glm::mat4 prevView = camera.getViewMatrix();

    cameraController.update(deltaTime);

    // Detect camera movement for DoF accumulation reset
    glm::mat4 currView = camera.getViewMatrix();
    bool cameraMovedThisFrame = (prevView != currView);

    if (cameraMovedThisFrame && dofEnabled_)
    {
        frameIndex_ = 0;  // Reset accumulation when camera moves
    }

    // Update camera UBO
    CameraUBO cameraUBO = buildCameraUBO();
    renderer.updateCamera(cameraUBO);

    // Increment frame index for DoF accumulation (if enabled and camera stationary)
    if (dofEnabled_ && !cameraMovedThisFrame)
    {
        frameIndex_++;
    }
}


void GRTScene::onResize(uint32_t width, uint32_t height)
{
    if (!initialized)
    {
        return;
    }

    if (width == 0 || height == 0)
    {
        return;
    }

    // Update camera aspect ratio
    camera.setAspect(width, height);

    // Resize renderer output
    renderer.resize(width, height);

    // Reset DoF accumulation on resize
    frameIndex_ = 0;
}


bool GRTScene::loadSceneCPU(const std::filesystem::path& plyPath, std::atomic<float>* progress)
{
    if (!provider_)
    {
        Log::ERR("Scene") << "Provider not set. Call initialize() first.";
        return false;
    }

    // 1. Load Gaussian data from PLY (CPU-only, thread-safe)
    Loader loader;
    if (!loader.loadPLY(plyPath, gaussianParticleData, progress))
    {
        Log::ERR("Scene") << "Failed to load PLY: " << loader.getLastError();
        return false;
    }

    dataLoaded = true;

    // 2. Compute scene bounds (CPU-only)
    computeSceneBounds();

    Log::INFO("Scene") << "Bounds: ("
        << std::fixed << std::setprecision(2)
        << minBound.x << ", " << minBound.y << ", " << minBound.z << ") to ("
        << maxBound.x << ", " << maxBound.y << ", " << maxBound.z << ")";

    return true;
}


bool GRTScene::loadSceneGPUStep(int step, GLFWwindow* window)
{
    if (!provider_)
    {
        Log::ERR("Scene") << "Provider not set.";
        return false;
    }

    VkContext* context = &provider_->getContext();

    switch (step)
    {
    case 0:  // Upload GPU buffers
    {
        if (!dataLoaded)
            break;

        VkQueue transferQueue         = context->queues[QueueType::TRANSFER];
        VkCommandPool transferCmdPool = provider_->getCommandPool(QueueType::TRANSFER);

        if (!gaussianParticleBuffers.initialize(context, gaussianParticleData, transferQueue, transferCmdPool))
        {
            Log::ERR("Scene") << "Failed to initialize GPU buffers";
            return false;
        }

        shDegree_ = gaussianParticleBuffers.getSHDegree();
        break;
    }
    case 1:  // Build BLAS
    {
        if (!dataLoaded)
            break;

        if (!blas.buildAndSubmit(provider_, gaussianParticleBuffers))
        {
            Log::ERR("Scene") << "Failed to build BLAS";
            return false;
        }
        break;
    }
    case 2:  // Build TLAS
    {
        if (!dataLoaded)
            break;

        if (!tlas.buildAndSubmit(provider_, gaussianParticleData, blas.getDeviceAddress()))
        {
            Log::ERR("Scene") << "Failed to build TLAS";
            return false;
        }
        break;
    }
    case 3:  // Initialize renderer + update descriptors
    {
        VkSwapchain& swapchain = provider_->getSwapchain();
        std::string shaderPath = std::string(SHADER_DIR) + "/";

        if (!renderer.initialize(context, swapchain.extent.width, swapchain.extent.height, shaderPath))
        {
            Log::ERR("Scene") << "Failed to initialize renderer";
            return false;
        }

        if (dataLoaded)
        {
            if (!renderer.updateDescriptors(tlas, gaussianParticleBuffers))
            {
                Log::ERR("Scene") << "Failed to update descriptors";
                return false;
            }

            SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
            renderer.updateSceneBounds(boundsUBO);
        }
        break;
    }
    case 4:  // Initialize camera
    {
        VkSwapchain& swapchain = provider_->getSwapchain();
        camera.setAspect(swapchain.extent.width, swapchain.extent.height);

        if (dataLoaded)
        {
            camera.position = glm::vec3(0.0f, 0.0f, 2.0f);
            camera.target   = glm::vec3(0.0f, 0.0f, 0.0f);
            camera.up       = glm::vec3(0.0f, 1.0f, 0.0f);

            cameraController.initialize(window, &camera);
        }
        else
        {
            cameraController.initialize(window, &camera);
            cameraController.resetToDefault();
        }

        CameraUBO cameraUBO = buildCameraUBO();
        renderer.updateCamera(cameraUBO);

        initialized = true;
        break;
    }
    default:
        return false;
    }

    return true;
}


bool GRTScene::initializeEmpty(GLFWwindow* window)
{
    if (!provider_)
    {
        Log::ERR("Scene") << "Provider not set. Call initialize() first.";
        return false;
    }

    VkContext* context = &provider_->getContext();

    // Initialize renderer
    VkSwapchain& swapchain = provider_->getSwapchain();
    std::string shaderPath = std::string(SHADER_DIR) + "/";

    if (!renderer.initialize(context, swapchain.extent.width, swapchain.extent.height, shaderPath))
    {
        Log::ERR("Scene") << "Failed to initialize renderer";
        return false;
    }

    // Initialize camera
    camera.setAspect(swapchain.extent.width, swapchain.extent.height);
    cameraController.initialize(window, &camera);
    cameraController.resetToDefault();

    CameraUBO cameraUBO = buildCameraUBO();
    renderer.updateCamera(cameraUBO);

    initialized = true;
    dataLoaded  = false;

    return true;
}


void GRTScene::setFisheyeParams(float fov,
                                float maxAngle,
                                float cx,
                                float cy,
                                float k1,
                                float k2,
                                float k3,
                                float k4)
{
    fisheyeFov_      = fov;
    fisheyeMaxAngle_ = maxAngle;
    fisheyeCx_       = cx;
    fisheyeCy_       = cy;
    fisheyeK1_       = k1;
    fisheyeK2_       = k2;
    fisheyeK3_       = k3;
    fisheyeK4_       = k4;
}


void GRTScene::setDoFEnabled(bool enabled)
{
    if (dofEnabled_ != enabled)
    {
        dofEnabled_ = enabled;
        frameIndex_ = 0;  // Reset accumulation when toggling DoF
    }
}


void GRTScene::setDoFParams(float aperture, float focalDistance)
{
    if (dofAperture_ != aperture || dofFocalDistance_ != focalDistance)
    {
        dofAperture_      = aperture;
        dofFocalDistance_ = focalDistance;
        frameIndex_       = 0;  // Reset accumulation when params change
    }
}


void GRTScene::setVisualizeMode(uint32_t mode)
{
    if (visualizeMode_ != mode)
    {
        visualizeMode_ = mode;

        // Update scene bounds UBO with new visualize mode
        if (initialized)
        {
            SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
            renderer.updateSceneBounds(boundsUBO);
        }
    }
}


void GRTScene::setRenderMode(uint32_t mode)
{
    if (renderMode_ != mode)
    {
        renderMode_ = mode;

        // Update scene bounds UBO with new render mode
        if (initialized)
        {
            SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
            renderer.updateSceneBounds(boundsUBO);
        }
    }
}


void GRTScene::setSHDegree(uint32_t degree)
{
    if (shDegree_ != degree)
    {
        shDegree_ = degree;

        // Update scene bounds UBO with new SH degree
        if (initialized)
        {
            SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
            renderer.updateSceneBounds(boundsUBO);
        }
    }
}


bool GRTScene::addMesh(MeshPreset preset,
                       const glm::vec3& position,
                       const glm::vec3& scale)
{
    if (!initialized || !dataLoaded)
    {
        return false;
    }

    // 1. Create CPU mesh
    MeshInstance mesh = createPresetMesh(preset, position, scale);
    meshInstances_.push_back(std::move(mesh));

    // 2. Rebuild GPU resources (buffers + BLAS + TLAS + descriptors)
    return rebuildMeshResources();
}


bool GRTScene::removeMesh(uint32_t index)
{
    if (index >= meshInstances_.size())
    {
        return false;
    }

    meshInstances_.erase(meshInstances_.begin() + index);

    if (meshInstances_.empty())
    {
        // All meshes removed — cleanup mesh resources
        VkContext* context = &provider_->getContext();
        vkDeviceWaitIdle(context->getDevice());

        meshTlas_.cleanup(context);
        meshBlas_.cleanup(context);
        meshBuffers_.cleanup(context->getAllocator());

        // Update descriptors (mesh bindings cleared)
        renderer.markDescriptorsDirty();
        renderer.updateDescriptors(tlas, gaussianParticleBuffers, nullptr, nullptr);

        // Update SceneBounds UBO (meshCount = 0)
        SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
        renderer.updateSceneBounds(boundsUBO);
        return true;
    }

    return rebuildMeshResources();
}


bool GRTScene::rebuildMeshResources()
{
    VkContext* context = &provider_->getContext();

    // Wait for GPU idle before rebuilding
    vkDeviceWaitIdle(context->getDevice());

    // 1. Cleanup existing mesh resources
    meshTlas_.cleanup(context);
    meshBlas_.cleanup(context);
    meshBuffers_.cleanup(context->getAllocator());

    // 2. Create GPU buffers
    VkQueue transferQueue         = context->queues[QueueType::TRANSFER];
    VkCommandPool transferCmdPool = provider_->getCommandPool(QueueType::TRANSFER);

    if (!meshBuffers_.initialize(context, meshInstances_, transferQueue, transferCmdPool))
    {
        Log::ERR("Scene") << "Failed to initialize mesh buffers";
        return false;
    }

    // 3. Build mesh BLAS
    if (!meshBlas_.buildAndSubmit(provider_, meshBuffers_))
    {
        Log::ERR("Scene") << "Failed to build mesh BLAS";
        return false;
    }

    // 4. Build Mesh TLAS (separate from Gaussian TLAS)
    auto meshTLASInstances = buildMeshTLASInstances();
    if (!meshTlas_.buildAndSubmit(provider_, meshTLASInstances))
    {
        Log::ERR("Scene") << "Failed to build mesh TLAS";
        return false;
    }

    // 5. Update descriptors (Mesh TLAS + mesh buffer bindings)
    renderer.markDescriptorsDirty();
    renderer.updateDescriptors(tlas, gaussianParticleBuffers,
                               &meshTlas_, &meshBuffers_);

    // 6. Update SceneBounds UBO (meshCount)
    SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
    renderer.updateSceneBounds(boundsUBO);

    return true;
}


std::vector<MeshTLASInstance> GRTScene::buildMeshTLASInstances() const
{
    std::vector<MeshTLASInstance> result;
    result.reserve(meshInstances_.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(meshInstances_.size()); ++i)
    {
        MeshTLASInstance inst;
        inst.blasAddress = meshBlas_.getDeviceAddress(i);
        inst.transform   = meshInstances_[i].transform;
        inst.meshIndex   = i;
        result.push_back(inst);
    }

    return result;
}


void GRTScene::updateMeshTransform(uint32_t index, const MeshTransform& transform)
{
    if (index >= meshInstances_.size())
    {
        return;
    }

    meshInstances_[index].meshTransform = transform;
    meshInstances_[index].transform     = transform.toMatrix();
    meshTlasDirty_ = true;
}


void GRTScene::setMeshMaterial(uint32_t index, MeshMaterialType type)
{
    if (index >= meshInstances_.size())
    {
        return;
    }

    if (meshInstances_[index].material.type == type)
    {
        return;
    }

    meshInstances_[index].material.type = type;

    // Rebuild mesh GPU resources and update UBO
    if (initialized)
    {
        rebuildMeshResources();
        SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
        renderer.updateSceneBounds(boundsUBO);
    }
}


void GRTScene::setReflectionEnabled(bool enabled)
{
    if (reflectionEnabled_ != enabled)
    {
        reflectionEnabled_ = enabled;
        if (initialized)
        {
            SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
            renderer.updateSceneBounds(boundsUBO);
        }
    }
}


void GRTScene::setMaxBounces(uint32_t bounces)
{
    if (maxBounces_ != bounces)
    {
        maxBounces_ = bounces;
        if (initialized)
        {
            SceneBoundsUBO boundsUBO = buildSceneBoundsUBO();
            renderer.updateSceneBounds(boundsUBO);
        }
    }
}


void GRTScene::computeSceneBounds()
{
    if (gaussianParticleData.particles.empty())
    {
        minBound = glm::vec3(-1.0f);
        maxBound = glm::vec3(1.0f);
        return;
    }

    minBound = glm::vec3(std::numeric_limits<float>::max());
    maxBound = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& particle : gaussianParticleData.particles)
    {
        // Account for scale when computing bounds
        float maxScale = std::max({particle.scale.x, particle.scale.y, particle.scale.z});
        float kernelScale = computeKernelScale(particle.opacity);
        float particleRadius = maxScale * kernelScale * 3.0f;  // 3-sigma bound

        minBound = glm::min(minBound, particle.position - glm::vec3(particleRadius));
        maxBound = glm::max(maxBound, particle.position + glm::vec3(particleRadius));
    }

    // Add small padding
    glm::vec3 padding = (maxBound - minBound) * 0.1f;
    minBound -= padding;
    maxBound += padding;
}


CameraUBO GRTScene::buildCameraUBO() const
{
    CameraUBO ubo{};

    ubo.viewInverse     = camera.getInverseViewMatrix();
    ubo.projInverse     = camera.getInverseProjectionMatrix();
    ubo.nearPlane       = camera.zNear;
    ubo.farPlane        = camera.zFar;
    ubo.cameraType      = cameraType_;
    ubo.fisheyeFov      = fisheyeFov_;
    ubo.fisheyeMaxAngle = fisheyeMaxAngle_;
    ubo.fisheyeCx       = fisheyeCx_;
    ubo.fisheyeCy       = fisheyeCy_;
    ubo.fisheyeK1       = fisheyeK1_;
    ubo.fisheyeK2       = fisheyeK2_;
    ubo.fisheyeK3       = fisheyeK3_;
    ubo.fisheyeK4       = fisheyeK4_;
    ubo.enableDoF       = dofEnabled_ ? 1u : 0u;
    ubo.aperture        = dofAperture_;
    ubo.focalDistance   = dofFocalDistance_;
    ubo.frameIndex      = frameIndex_;

    return ubo;
}


SceneBoundsUBO GRTScene::buildSceneBoundsUBO() const
{
    SceneBoundsUBO ubo{};

    // Existing fields
    ubo.minBound     = minBound;
    ubo.tMin         = 0.001f;
    ubo.maxBound     = maxBound;
    ubo.tMax         = glm::length(maxBound - minBound) * 2.0f;
    ubo.hasSH        = gaussianParticleBuffers.hasSHCoefficients() ? 1 : 0;
    ubo.visualizeMode = visualizeMode_;
    ubo.shDegree     = shDegree_;
    ubo.kernelDegree = kernelDegree_;

    // Reflection parameters (derived from per-mesh material types)
    bool anyReflective = false;
    for (const auto& mesh : meshInstances_)
    {
        if (mesh.material.type == MeshMaterialType::REFLECTIVE)
        {
            anyReflective = true;
            break;
        }
    }
    ubo.enableReflection = anyReflective ? 1 : 0;
    ubo.maxBounces       = maxBounces_;
    ubo.meshCount        = static_cast<uint32_t>(meshInstances_.size());
    ubo.renderMode       = renderMode_;

    // Lighting parameters (always enabled with default values)
    ubo.enableLighting    = 1;
    ubo.enableSpecular    = 1;
    ubo.specularShininess = 32.0f;
    ubo._pad1             = 0;
    ubo.lightDir          = glm::vec3(0.0f, -1.0f, 0.0f);
    ubo.lightIntensity    = 1.0f;
    ubo.lightColor        = glm::vec3(1.0f);
    ubo.ambientIntensity  = 0.2f;
    ubo.ambientColor      = glm::vec3(1.0f);
    ubo._pad2             = 0;

    return ubo;
}

}   // namespace vk3dgrt