#include "scene-manager.h"

#include "log.h"
#include "3dgrt/grt-scene.h"


SceneManager::~SceneManager()
{
    if (loadingThread_ && loadingThread_->joinable())
    {
        loadingThread_->join();
    }
}


void SceneManager::initialize(VkProvider* provider, GLFWwindow* window)
{
    provider_ = provider;
    window_   = window;
}


void SceneManager::cleanup()
{
    // Wait for any loading thread to complete
    if (loadingThread_ && loadingThread_->joinable())
    {
        loadingThread_->join();
    }
    loadingThread_.reset();
    pendingScene_.reset();
    loadingStage_.store(LoadingStage::IDLE);

    unloadScene();
    provider_ = nullptr;
    window_   = nullptr;
}


void SceneManager::loadGRTScene(const std::filesystem::path& plyPath)
{
    if (!provider_)
    {
        Log::ERR("Scene") << "Not initialized";
        return;
    }

    // Wait for any previous loading thread
    if (loadingThread_ && loadingThread_->joinable())
    {
        loadingThread_->join();
    }
    loadingThread_.reset();
    pendingScene_.reset();

    // Unload existing scene
    unloadScene();

    // Reset loading state
    loadingStage_.store(LoadingStage::LOADING_PLY);
    loadingProgress_.store(0.0f);
    cpuLoadDone_.store(false);
    cpuLoadFailed_.store(false);
    gpuLoadStep_ = 0;
    loadingFileName_ = plyPath.filename().string();

    // Create and initialize GRT scene
    pendingScene_ = std::make_unique<vk3dgrt::GRTScene>();
    if (!pendingScene_->initialize(provider_))
    {
        Log::ERR("Scene") << "Failed to initialize GRT scene";
        loadingStage_.store(LoadingStage::FAILED);
        pendingScene_.reset();
        return;
    }

    // Launch background thread for CPU work
    loadingThread_ = std::make_unique<std::thread>(
        [this, plyPath]()
        {
            bool result = pendingScene_->loadSceneCPU(plyPath, &loadingProgress_);
            if (result)
            {
                cpuLoadDone_.store(true, std::memory_order_release);
            }
            else
            {
                cpuLoadFailed_.store(true, std::memory_order_release);
            }
        });

    Log::INFO("Scene") << "Async loading started: " << loadingFileName_;
}


bool SceneManager::isLoading() const
{
    LoadingStage stage = loadingStage_.load(std::memory_order_acquire);
    return stage != LoadingStage::IDLE &&
           stage != LoadingStage::COMPLETE &&
           stage != LoadingStage::FAILED;
}


float SceneManager::getLoadingProgress() const
{
    LoadingStage stage = loadingStage_.load(std::memory_order_acquire);

    if (stage == LoadingStage::LOADING_PLY)
    {
        // PLY loading progress maps to 0.0 - 0.70
        return loadingProgress_.load(std::memory_order_relaxed) * 0.70f;
    }

    // Progress ranges for GPU steps
    constexpr float kGpuStepStart[] = {0.70f, 0.80f, 0.86f, 0.92f, 0.97f};
    constexpr float kGpuStepEnd[]   = {0.80f, 0.86f, 0.92f, 0.97f, 1.00f};

    int stageIndex = static_cast<int>(stage) - static_cast<int>(LoadingStage::UPLOADING_BUFFERS);
    if (stageIndex >= 0 && stageIndex < 5)
    {
        return kGpuStepStart[stageIndex];
    }

    if (stage == LoadingStage::COMPLETE)
    {
        return 1.0f;
    }

    return 0.0f;
}


const char* SceneManager::getLoadingStageName() const
{
    switch (loadingStage_.load(std::memory_order_acquire))
    {
    case LoadingStage::LOADING_PLY:       return "Loading PLY file...";
    case LoadingStage::UPLOADING_BUFFERS: return "Uploading GPU buffers...";
    case LoadingStage::BUILDING_BLAS:     return "Building BLAS...";
    case LoadingStage::BUILDING_TLAS:     return "Building TLAS...";
    case LoadingStage::INIT_RENDERER:     return "Initializing renderer...";
    case LoadingStage::SETTING_UP_CAMERA: return "Setting up camera...";
    case LoadingStage::COMPLETE:          return "Complete";
    case LoadingStage::FAILED:            return "Failed";
    default:                              return "";
    }
}


void SceneManager::updateLoading()
{
    LoadingStage stage = loadingStage_.load(std::memory_order_acquire);
    if (stage == LoadingStage::IDLE || stage == LoadingStage::COMPLETE || stage == LoadingStage::FAILED)
    {
        return;
    }

    // Check for CPU loading failure
    if (cpuLoadFailed_.load(std::memory_order_acquire))
    {
        if (loadingThread_ && loadingThread_->joinable())
        {
            loadingThread_->join();
        }
        loadingThread_.reset();
        pendingScene_.reset();
        loadingStage_.store(LoadingStage::FAILED);
        Log::ERR("Scene") << "Async loading failed during CPU phase";
        return;
    }

    // Wait for CPU work to complete before starting GPU work
    if (stage == LoadingStage::LOADING_PLY)
    {
        if (!cpuLoadDone_.load(std::memory_order_acquire))
        {
            return;  // Still loading PLY on background thread
        }

        // Join the background thread
        if (loadingThread_ && loadingThread_->joinable())
        {
            loadingThread_->join();
        }
        loadingThread_.reset();

        // Transition to GPU loading phase
        gpuLoadStep_ = 0;
        loadingStage_.store(LoadingStage::UPLOADING_BUFFERS);
        return;  // Will process GPU step next frame
    }

    // GPU loading: one step per frame
    static constexpr LoadingStage kGpuStages[] = {
        LoadingStage::UPLOADING_BUFFERS,
        LoadingStage::BUILDING_BLAS,
        LoadingStage::BUILDING_TLAS,
        LoadingStage::INIT_RENDERER,
        LoadingStage::SETTING_UP_CAMERA
    };

    if (gpuLoadStep_ < vk3dgrt::GRTScene::GPU_LOAD_STEP_COUNT)
    {
        loadingStage_.store(kGpuStages[gpuLoadStep_]);

        if (!pendingScene_->loadSceneGPUStep(gpuLoadStep_, window_))
        {
            Log::ERR("Scene") << "Async loading failed at GPU step " << gpuLoadStep_;
            pendingScene_.reset();
            loadingStage_.store(LoadingStage::FAILED);
            return;
        }

        gpuLoadStep_++;

        if (gpuLoadStep_ >= vk3dgrt::GRTScene::GPU_LOAD_STEP_COUNT)
        {
            // All steps complete - move pending scene to current
            currentScene_ = std::move(pendingScene_);
            loadingStage_.store(LoadingStage::COMPLETE);

            auto* gs = dynamic_cast<vk3dgrt::GRTScene*>(currentScene_.get());
            if (gs)
            {
                Log::OK("Scene") << "Ready — "
                    << Log::formatCount(gs->getParticleCount()) << " gaussians";
            }
        }
    }
}


void SceneManager::unloadScene()
{
    if (currentScene_)
    {
        currentScene_->cleanup();
        currentScene_.reset();
    }
}


bool SceneManager::hasScene() const
{
    if (!currentScene_ || !currentScene_->isInitialized())
    {
        return false;
    }

    // Check GRTScene-specific data readiness
    auto* gs = dynamic_cast<vk3dgrt::GRTScene*>(currentScene_.get());
    if (gs)
    {
        return gs->hasData();
    }

    return true;
}


void SceneManager::update(float deltaTime)
{
    if (currentScene_ && currentScene_->isInitialized())
    {
        currentScene_->update(deltaTime);
    }
}


void SceneManager::recordCommands(VkCommandBuffer cmd)
{
    if (currentScene_ && currentScene_->isInitialized())
    {
        currentScene_->recordCommands(cmd);
    }
}


void SceneManager::copyToSwapchain(VkCommandBuffer cmd, VkImage dstImage, VkExtent2D dstExtent)
{
    if (currentScene_ && currentScene_->isInitialized())
    {
        currentScene_->copyToSwapchain(cmd, dstImage, dstExtent);
    }
}


void SceneManager::onResize(uint32_t width, uint32_t height)
{
    if (currentScene_ && currentScene_->isInitialized())
    {
        currentScene_->onResize(width, height);
    }
}


Scene* SceneManager::getCurrentScene() const
{
    return currentScene_.get();
}


vk3dgrt::GRTScene* SceneManager::getGRTScene() const
{
    return dynamic_cast<vk3dgrt::GRTScene*>(currentScene_.get());
}
