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


struct RtProperties
{
    const uint32_t shaderGroupHandleSize                          = 32;
    uint32_t       shaderGroupHandleAlignment                     = 0;
    uint32_t       shaderGroupBaseAlignment                       = 0;
    uint32_t       minAccelerationStructureScratchOffsetAlignment = 0;
    const uint32_t asBufferOffsetAlignment                        = 256;
};


struct VkContext
{
    VkInstance   instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface  = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;

    VmaAllocator allocator = VK_NULL_HANDLE;

    RtProperties rtProps;

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
    VmaAllocator     getAllocator() const      { return allocator; }
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


extern PFN_vkGetBufferDeviceAddressKHR                vkGetBufferDeviceAddressKHR_;
extern PFN_vkCreateAccelerationStructureKHR           vkCreateAccelerationStructureKHR_;
extern PFN_vkDestroyAccelerationStructureKHR          vkDestroyAccelerationStructureKHR_;
extern PFN_vkGetAccelerationStructureBuildSizesKHR    vkGetAccelerationStructureBuildSizesKHR_;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_;
extern PFN_vkCmdBuildAccelerationStructuresKHR        vkCmdBuildAccelerationStructuresKHR_;
extern PFN_vkCreateRayTracingPipelinesKHR             vkCreateRayTracingPipelinesKHR_;
extern PFN_vkGetRayTracingShaderGroupHandlesKHR       vkGetRayTracingShaderGroupHandlesKHR_;
extern PFN_vkCmdTraceRaysKHR                          vkCmdTraceRaysKHR_;


#endif // VKCONTEXT_H