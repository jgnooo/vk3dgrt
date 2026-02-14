#include "scene-manager.h"

#include "3dgrt/grt-scene.h"

#include <iostream>


void SceneManager::initialize(VkProvider* provider, GLFWwindow* window)
{
    provider_ = provider;
    window_   = window;
}


void SceneManager::cleanup()
{
    unloadScene();
    provider_ = nullptr;
    window_   = nullptr;
}


bool SceneManager::loadGRTScene(const std::filesystem::path& plyPath)
{
    if (!provider_)
    {
        std::cerr << "[SceneManager] Not initialized" << std::endl;
        return false;
    }

    // Unload existing scene
    unloadScene();

    // Create and initialize GRT scene
    auto scene = std::make_unique<vk3dgrt::GRTScene>();
    if (!scene->initialize(provider_))
    {
        std::cerr << "[SceneManager] Failed to initialize GRT scene" << std::endl;
        return false;
    }

    if (!scene->loadScene(plyPath, window_))
    {
        std::cerr << "[SceneManager] Failed to load GRT scene" << std::endl;
        return false;
    }

    currentScene_ = std::move(scene);
    return true;
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