#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "scene.h"

#include <filesystem>
#include <memory>


// Forward declarations
class VkProvider;
struct GLFWwindow;


namespace vk3dgrt { class GRTScene; }


class SceneManager
{
    VkProvider* provider_ = nullptr;
    GLFWwindow* window_   = nullptr;

    std::unique_ptr<Scene> currentScene_;

public:
    SceneManager()  = default;
    ~SceneManager() = default;

    // Disable copy
    SceneManager(const SceneManager&)            = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void initialize(VkProvider* provider, GLFWwindow* window);

    void cleanup();

    bool loadGRTScene(const std::filesystem::path& plyPath);

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