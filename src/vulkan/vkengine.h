#ifndef VKENGINE_H
#define VKENGINE_H

// Include all Vulkan module headers
#include "vkprovider.h"
#include "vkbuffer.h"
#include "vkimage.h"
#include "vkpipeline.h"
#include "vkaccelstruct.h"

#include "gui/gui.h"
#include "scene-manager.h"

#include <array>
#include <filesystem>
#include <map>


// Vulkan timeout constants
constexpr uint64_t kFenceTimeoutNs   = 5'000'000'000;  // 5 seconds (heavy RT scenes)
constexpr uint64_t kAcquireTimeoutNs = 1'000'000'000;  // 1 second

// Maximum frames in flight for CPU/GPU overlap
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;


struct GLFWwindow;


// Per-frame synchronization data for multi-buffered rendering
struct FrameData
{
    VkCommandBuffer commandBuffer  = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    VkFence         inFlightFence  = VK_NULL_HANDLE;
};


class VkEngine : public VkProvider
{
    bool isInitialized      = false;
    bool framebufferResized = false;

    GLFWwindow* window = nullptr;

    VkContext   context;
    VkSwapchain swapchain;

    std::map<QueueType, VkCommandPool> commandPools;

    // Multi-frame buffering for CPU/GPU overlap
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> mFrames;
    uint32_t mCurrentFrame = 0;

    // Per-swapchain-image semaphores to avoid presentation semaphore reuse
    std::vector<VkSemaphore> mRenderFinishedSemaphores;

    // ImGui
    ImGuiManager imguiManager;

    // Scene Manager
    SceneManager sceneManager;

    // Timing
    float lastFrameTime = 0.0f;
    float deltaTime     = 0.0f;

public:
    void initialize();
    void cleanup();
    void run();

    SceneManager& getSceneManager() { return sceneManager; }

    GLFWwindow* getWindow() { return window; }

    VkContext&    getContext() override                   { return context; }
    VkSwapchain&  getSwapchain() override                 { return swapchain; }
    VkCommandPool getCommandPool(QueueType type) override { return commandPools[type]; }

    VkQueue         getQueue(QueueType type) override;
    VkExtent2D      getSwapchainExtent() override;
    VkFormat        getSwapchainFormat() override;
    VkCommandBuffer beginSingleTimeCommands(QueueType type) override;
    void            endSingleTimeCommands(VkCommandBuffer cmd, QueueType type) override;

    // Current frame accessors (backward compatible)
    FrameData&      getCurrentFrame()                         { return mFrames[mCurrentFrame]; }
    VkCommandBuffer getCurrentCmdBuffer()                     { return mFrames[mCurrentFrame].commandBuffer; }
    VkFence         getCurrentFence()                         { return mFrames[mCurrentFrame].inFlightFence; }
    VkSemaphore     getImageAvailableSem()                    { return mFrames[mCurrentFrame].imageAvailable; }
    VkSemaphore     getRenderFinishedSem(uint32_t imageIndex) { return mRenderFinishedSemaphores[imageIndex]; }
    uint32_t        getCurrentFrameIndex()                    { return mCurrentFrame; }

private:
    void createGLFWWindow();
    void createCommandPools();
    void createFrameResources();

    void recreateSwapchain();
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void draw();

    bool acquire(uint32_t& imageIndex);
    void record(VkCommandBuffer cmdBuffer, uint32_t imageIndex);
    void submit(VkCommandBuffer cmdBuffer, uint32_t imageIndex);
    bool present(uint32_t imageIndex);

    void transitionImageLayout(VkCommandBuffer cmdBuffer,
                               VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage,
                               VkAccessFlags srcAccess,
                               VkAccessFlags dstAccess);
};

#endif // VKENGINE_H