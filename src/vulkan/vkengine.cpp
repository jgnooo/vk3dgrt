#include "vkengine.h"

#include "vkerror.h"
#include "log.h"
#include "3dgrt/grt-scene.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <GLFW/glfw3.h>

#include <random>
#include <stdexcept>
#include <vector>


void VkEngine::initialize()
{
    if (isInitialized)
        throw std::runtime_error("[VkEngine] Vulkan engine is already initialized.");

    createGLFWWindow();

    context.initialize(window);
    // TODO: Check hardcoded size.
    swapchain.create(&context, 1280, 720);

    createCommandPools();
    createFrameResources();

    // Initialize ImGui
    imguiManager.initialize(window, &context, &swapchain);

    // Initialize SceneManager
    sceneManager.initialize(this, window);

    isInitialized = true;

    Log::OK("Engine") << "Initialized (" << swapchain.extent.width << "x" << swapchain.extent.height << ")";
}


void VkEngine::cleanup()
{
    if (isInitialized)
    {
        // Wait for all operations to complete
        vkDeviceWaitIdle(context.getDevice());

        // Cleanup scene manager
        sceneManager.cleanup();

        // Shutdown ImGui
        imguiManager.shutdown();

        // Cleanup per-swapchain-image semaphores
        for (auto sem : mRenderFinishedSemaphores)
            vkDestroySemaphore(context.getDevice(), sem, nullptr);
        mRenderFinishedSemaphores.clear();

        // Cleanup per-frame resources
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroyFence(context.getDevice(), mFrames[i].inFlightFence, nullptr);
            vkDestroySemaphore(context.getDevice(), mFrames[i].imageAvailable, nullptr);
            // Command buffers are freed when command pool is destroyed
        }

        for (const auto& [type, pool] : commandPools)
            vkDestroyCommandPool(context.getDevice(), pool, nullptr);
        commandPools.clear();

        swapchain.cleanup(context.getDevice());
        context.cleanup();

        if (window)
            glfwDestroyWindow(window);

        glfwTerminate();
    }
}


void VkEngine::run()
{
    if (!isInitialized)
        throw std::runtime_error("[VkEngine] Vulkan engine is not initialized.");

    // Initialize timing
    lastFrameTime = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window))
    {
        // Calculate delta time
        float currentTime = static_cast<float>(glfwGetTime());
        deltaTime         = currentTime - lastFrameTime;
        lastFrameTime     = currentTime;

        glfwPollEvents();

        // Update scene (camera, etc.)
        sceneManager.update(deltaTime);

        draw();
    }
}


VkQueue VkEngine::getQueue(QueueType type)
{
    return context.queues[type];
}


VkExtent2D VkEngine::getSwapchainExtent()
{
    return swapchain.extent;
}


VkFormat VkEngine::getSwapchainFormat()
{
    return swapchain.format;
}


VkCommandBuffer VkEngine::beginSingleTimeCommands(QueueType type)
{
    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPools[type],
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmdBuffer;
    VK_CHECK(vkAllocateCommandBuffers(context.getDevice(), &allocInfo, &cmdBuffer));

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));
    return cmdBuffer;
}


void VkEngine::endSingleTimeCommands(VkCommandBuffer cmd, QueueType type)
{
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd
    };

    VK_CHECK(vkQueueSubmit(context.queues[type], 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(context.queues[type]));

    vkFreeCommandBuffers(context.getDevice(), commandPools[type], 1, &cmd);
}


void VkEngine::createGLFWWindow()
{
    if (!glfwInit())
        throw std::runtime_error("[VkEngine] Failed to initialize GLFW.");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(1280, 720, "VK3DGRT", nullptr, nullptr);

    if (!window)
        throw std::runtime_error("[VkEngine] Failed to create GLFW window.");

    // Set up framebuffer resize callback
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}


void VkEngine::createCommandPools()
{
    std::vector<QueueType> queueTypes = {
        QueueType::GRAPHICS,
        QueueType::COMPUTE,
        QueueType::TRANSFER
    };

    for (const auto& type : queueTypes)
    {
        VkCommandPoolCreateInfo poolInfo{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = context.queueFamilyIndices[type]
        };

        VkCommandPool commandPool;
        VK_CHECK(vkCreateCommandPool(context.getDevice(), &poolInfo, nullptr, &commandPool));
        commandPools[type] = commandPool;
    }
}


void VkEngine::createFrameResources()
{
    VkCommandPool graphicsPool = commandPools[QueueType::GRAPHICS];

    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = graphicsPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };

    // Allocate command buffers for all frames at once
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> cmdBuffers;
    VK_CHECK(vkAllocateCommandBuffers(context.getDevice(), &allocInfo, cmdBuffers.data()));

    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    // Create synchronization primitives for each frame
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        mFrames[i].commandBuffer = cmdBuffers[i];

        VK_CHECK(vkCreateFence(context.device, &fenceInfo, nullptr, &mFrames[i].inFlightFence));
        VK_CHECK(vkCreateSemaphore(context.device, &semaphoreInfo, nullptr, &mFrames[i].imageAvailable));
    }

    // Create per-swapchain-image semaphores for presentation synchronization.
    // Indexed by imageIndex so a semaphore is only reused when the same image is re-acquired,
    // guaranteeing the presentation engine has released it.
    uint32_t imageCount = static_cast<uint32_t>(swapchain.images.size());
    mRenderFinishedSemaphores.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VK_CHECK(vkCreateSemaphore(context.device, &semaphoreInfo, nullptr, &mRenderFinishedSemaphores[i]));
    }
}


void VkEngine::recreateSwapchain()
{
    // Handle minimization (window size = 0)
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    // Wait for device to be idle before recreating
    vkDeviceWaitIdle(context.device);

    // Cleanup old per-image semaphores
    for (auto sem : mRenderFinishedSemaphores)
        vkDestroySemaphore(context.device, sem, nullptr);
    mRenderFinishedSemaphores.clear();

    // Cleanup old swapchain
    swapchain.cleanup(context.device);

    // Create new swapchain with new dimensions
    swapchain.create(&context, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    // Recreate per-image semaphores for new swapchain
    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    uint32_t imageCount = static_cast<uint32_t>(swapchain.images.size());
    mRenderFinishedSemaphores.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VK_CHECK(vkCreateSemaphore(context.device, &semaphoreInfo, nullptr, &mRenderFinishedSemaphores[i]));
    }

    // Notify scene about resize
    sceneManager.onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    framebufferResized = false;
}


void VkEngine::framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    auto engine = reinterpret_cast<VkEngine*>(glfwGetWindowUserPointer(window));
    engine->framebufferResized = true;
}


void VkEngine::draw()
{
    FrameData& frame = mFrames[mCurrentFrame];

    // Wait for THIS frame's previous work to complete (allows other frame to continue)
    VK_CHECK(vkWaitForFences(context.device, 1, &frame.inFlightFence, VK_TRUE, kFenceTimeoutNs));

    uint32_t imageIndex;
    if (!acquire(imageIndex))
    {
        // Swapchain out of date, recreate and skip this frame
        recreateSwapchain();
        return;
    }

    // Only reset fence after successful acquire to avoid deadlock
    VK_CHECK(vkResetFences(context.device, 1, &frame.inFlightFence));

    // Start ImGui frame
    imguiManager.newFrame();

    // Update GUI state from scene
    auto* gs = sceneManager.getGRTScene();
    imguiManager.setScene(gs);
    if (gs)
    {
        imguiManager.setSHAvailable(gs->hasSHData());
        imguiManager.setSHDegree(static_cast<int>(gs->getSHDegree()));
    }

    // Build ImGui UI
    imguiManager.showFpsOverlay();
    imguiManager.showRightPanel();

    // Handle render mode changes from GUI
    if (imguiManager.isRenderModeChanged())
    {
        auto* gs = sceneManager.getGRTScene();
        if (gs)
        {
            gs->setRenderMode(imguiManager.getRenderMode());
        }
        imguiManager.clearRenderModeChanged();
    }

    // Handle SH degree changes from GUI
    if (imguiManager.isSHDegreeChanged())
    {
        auto* gs = sceneManager.getGRTScene();
        if (gs)
        {
            gs->setSHDegree(static_cast<uint32_t>(imguiManager.getSHDegree()));
        }
        imguiManager.clearSHDegreeChanged();
    }

    // Handle reflection toggle from GUI
    if (imguiManager.isReflectionEnabledChanged())
    {
        auto* gs = sceneManager.getGRTScene();
        if (gs)
        {
            gs->setReflectionEnabled(imguiManager.isReflectionEnabled());
        }
        imguiManager.clearReflectionEnabledChanged();
    }

    // Handle max bounces changes from GUI
    if (imguiManager.isMaxBouncesChanged())
    {
        auto* gs = sceneManager.getGRTScene();
        if (gs)
        {
            gs->setMaxBounces(static_cast<uint32_t>(imguiManager.getMaxBounces()));
        }
        imguiManager.clearMaxBouncesChanged();
    }

    // Handle mesh insertion from GUI
    if (imguiManager.shouldInsertTeapot())
    {
        auto* gs = sceneManager.getGRTScene();
        if (gs)
        {
            static std::mt19937 rng(std::random_device{}());
            static std::uniform_real_distribution<float> dist(-7.0f, 7.0f);

            glm::vec3 randomPos(dist(rng), dist(rng), dist(rng));
            gs->addMesh(vk3dgrt::MeshPreset::TEAPOT, randomPos);
        }
        imguiManager.clearInsertTeapot();
    }

    // Handle mesh removal from GUI
    if (imguiManager.getRemoveMeshIndex() >= 0)
    {
        auto* gs = sceneManager.getGRTScene();
        if (gs)
        {
            gs->removeMesh(static_cast<uint32_t>(imguiManager.getRemoveMeshIndex()));
        }
        imguiManager.clearRemoveMeshIndex();
    }

    if (imguiManager.isCameraTypeChanged())
    {
        if (gs)
        {
            gs->setCameraType(imguiManager.getCameraType());
        }
        imguiManager.clearCameraTypeChanged();
    }

    if (imguiManager.isFisheyeParamsChanged())
    {
        if (gs)
        {
            gs->setFisheyeParams(
                imguiManager.getFisheyeFovDeg(),
                imguiManager.getFisheyeMaxAngleDeg(),
                imguiManager.getFisheyeCx(),
                imguiManager.getFisheyeCy(),
                imguiManager.getFisheyeK1(),
                imguiManager.getFisheyeK2(),
                imguiManager.getFisheyeK3(),
                imguiManager.getFisheyeK4()
            );
        }
        imguiManager.clearFisheyeParamsChanged();
    }

    // Finalize ImGui frame
    imguiManager.render();

    VkCommandBuffer cmdBuffer = frame.commandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmdBuffer, 0));

    record(cmdBuffer, imageIndex);
    submit(cmdBuffer, imageIndex);

    if (!present(imageIndex) || framebufferResized)
    {
        // Swapchain out of date or suboptimal, recreate
        recreateSwapchain();
    }

    // Advance to next frame
    mCurrentFrame = (mCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}


bool VkEngine::acquire(uint32_t& imageIndex)
{
    FrameData& frame = mFrames[mCurrentFrame];

    VkResult result = vkAcquireNextImageKHR(
        context.device,
        swapchain.swapchain,
        kAcquireTimeoutNs,
        frame.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return false;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        VK_CHECK(result);
    }

    return true;
}


void VkEngine::record(VkCommandBuffer cmdBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));

    // Check if we should render the scene
    bool renderScene = sceneManager.hasScene();

    if (renderScene)
    {
        // Record scene rendering commands
        sceneManager.recordCommands(cmdBuffer);

        // Copy scene output to swapchain image
        sceneManager.copyToSwapchain(cmdBuffer, swapchain.images[imageIndex], swapchain.extent);

        // Transition swapchain image for ImGui overlay: PRESENT_SRC_KHR -> COLOR_ATTACHMENT_OPTIMAL
        transitionImageLayout(
            cmdBuffer,
            swapchain.images[imageIndex],
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        );
    }
    else
    {
        // No scene loaded - just clear and render ImGui
        transitionImageLayout(
            cmdBuffer,
            swapchain.images[imageIndex],
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        );
    }

    // Setup dynamic rendering for ImGui overlay
    VkRenderingAttachmentInfo colorAttachment{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = swapchain.imageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = renderScene ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.color = {{0.1f, 0.1f, 0.2f, 1.0f}}}
    };

    VkRenderingInfo renderInfo{
        .sType      = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = {0, 0},
            .extent = swapchain.extent
        },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment
    };

    vkCmdBeginRendering(cmdBuffer, &renderInfo);

    // Render ImGui overlay
    imguiManager.renderDrawData(cmdBuffer);

    vkCmdEndRendering(cmdBuffer);

    // Transition swapchain image: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    transitionImageLayout(
        cmdBuffer,
        swapchain.images[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0
    );

    VK_CHECK(vkEndCommandBuffer(cmdBuffer));
}


void VkEngine::submit(VkCommandBuffer cmdBuffer, uint32_t imageIndex)
{
    FrameData& frame = mFrames[mCurrentFrame];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &frame.imageAvailable,
        .pWaitDstStageMask    = &waitStage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmdBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &mRenderFinishedSemaphores[imageIndex]
    };

    VK_CHECK(vkQueueSubmit(context.queues[QueueType::GRAPHICS], 1, &submitInfo, frame.inFlightFence));
}


bool VkEngine::present(uint32_t imageIndex)
{
    VkPresentInfoKHR presentInfo{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &mRenderFinishedSemaphores[imageIndex],
        .swapchainCount     = 1,
        .pSwapchains        = &swapchain.swapchain,
        .pImageIndices      = &imageIndex
    };

    VkResult result = vkQueuePresentKHR(context.queues[QueueType::GRAPHICS], &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        return false;
    }
    else if (result != VK_SUCCESS)
    {
        VK_CHECK(result);
    }

    return true;
}


void VkEngine::transitionImageLayout(VkCommandBuffer cmdBuffer,
                                     VkImage image,
                                     VkImageLayout oldLayout,
                                     VkImageLayout newLayout,
                                     VkPipelineStageFlags srcStage,
                                     VkPipelineStageFlags dstStage,
                                     VkAccessFlags srcAccess,
                                     VkAccessFlags dstAccess)
{
    VkImageMemoryBarrier barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = srcAccess,
        .dstAccessMask       = dstAccess,
        .oldLayout           = oldLayout,
        .newLayout           = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
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
        srcStage,
        dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}