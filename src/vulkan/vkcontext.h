#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <vulkan/vulkan_core.h>

#include <map>


struct GLFWwindow;


enum class QueueType
{
    GRAPHICS,
    COMPUTE,
    TRANSFER
};


struct VkContext
{
    VkInstance   instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface  = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;

    std::map<QueueType, VkQueue>  queues;
    std::map<QueueType, uint32_t> queueFamilyIndices;

    void initialize(GLFWwindow* window);
    void createInstance();
    void createSurface(GLFWwindow* window);
    void createDevice();
    void cleanup();
};

#endif // VKCONTEXT_H