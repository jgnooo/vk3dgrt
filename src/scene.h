#ifndef SCENE_H
#define SCENE_H

#include <vulkan/vulkan.h>

#include <cstdint>


class VkProvider;


class Scene
{
public:
    virtual bool initialize(VkProvider* provider)                                          = 0;
    virtual void cleanup()                                                                 = 0;
    virtual bool isInitialized() const                                                     = 0;
    virtual void recordCommands(VkCommandBuffer cmd)                                       = 0;
    virtual void copyToSwapchain(VkCommandBuffer cmd, VkImage dstImage, VkExtent2D dstExtent) = 0;
    virtual void update(float deltaTime)                                                   = 0;
    virtual void onResize(uint32_t width, uint32_t height)                                 = 0;

    virtual ~Scene() = default;
};


#endif // SCENE_H
