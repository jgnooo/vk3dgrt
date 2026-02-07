#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include <map>
#include <vector>


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

    VmaAllocator allocator = VK_NULL_HANDLE;

    std::map<QueueType, VkQueue>  queues;
    std::map<QueueType, uint32_t> queueFamilyIndices;

    void initialize(GLFWwindow* window);
    void createInstance();
    void createSurface(GLFWwindow* window);
    void createDevice();
    void createAllocator();
    void cleanup();

    // Getters
    VkSurfaceKHR     getSurface() const        { return surface; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice         getDevice() const         { return device; }
};


struct VkSwapchain
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat       format    = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent    = {0, 0};

    std::vector<VkImage>     images;
    std::vector<VkImageView> imageViews;

    void create(VkContext* context, uint32_t width, uint32_t height);
    void cleanup(VkDevice device);
};

#endif // VKCONTEXT_H