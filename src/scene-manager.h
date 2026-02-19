#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "scene.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>


// Forward declarations
class VkProvider;
struct GLFWwindow;


namespace vk3dgrt { class GRTScene; }


enum class LoadingStage : int
{
    IDLE              = 0,
    LOADING_PLY       = 1,
    UPLOADING_BUFFERS = 2,
    BUILDING_BLAS     = 3,
    BUILDING_TLAS     = 4,
    INIT_RENDERER     = 5,
    SETTING_UP_CAMERA = 6,
    COMPLETE          = 7,
    FAILED            = -1
};


class SceneManager
{
    VkProvider* provider_ = nullptr;
    GLFWwindow* window_   = nullptr;

    std::unique_ptr<Scene> currentScene_;

    // Async loading state
    std::unique_ptr<std::thread>        loadingThread_;
    std::unique_ptr<vk3dgrt::GRTScene>  pendingScene_;
    std::atomic<LoadingStage>           loadingStage_{LoadingStage::IDLE};
    std::atomic<float>                  loadingProgress_{0.0f};
    std::atomic<bool>                   cpuLoadDone_{false};
    std::atomic<bool>                   cpuLoadFailed_{false};
    int                                 gpuLoadStep_ = 0;
    std::string                         loadingFileName_;

public:
    SceneManager()  = default;
    ~SceneManager();

    // Disable copy
    SceneManager(const SceneManager&)            = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void initialize(VkProvider* provider, GLFWwindow* window);

    void cleanup();

    void loadGRTScene(const std::filesystem::path& plyPath);

    bool isLoading() const;

    float getLoadingProgress() const;

    const char* getLoadingStageName() const;

    const std::string& getLoadingFileName() const { return loadingFileName_; }

    // Called each frame to advance GPU loading steps when CPU work is done
    void updateLoading();

    void unloadScene();

    bool hasScene() const;

    void update(float deltaTime);

    void recordCommands(VkCommandBuffer cmd);

    void copyToSwapchain(VkCommandBuffer cmd, VkImage dstImage, VkExtent2D dstExtent);

    void onResize(uint32_t width, uint32_t height);

    Scene* getCurrentScene() const;

    vk3dgrt::GRTScene* getGRTScene() const;
};

#endif // SCENE_MANAGER_H
