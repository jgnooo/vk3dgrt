#ifndef VKIMAGE_H
#define VKIMAGE_H

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include <cstdint>


struct VkContext;


enum class ImageUsage
{
    COLOR_ATTACHMENT,
    DEPTH_ATTACHMENT,
    STORAGE,
    SAMPLED,
    TRANSFER_SRC,
    TRANSFER_DST
};


struct AllocatedImage
{
    VkImage           image      = VK_NULL_HANDLE;
    VkImageView       imageView  = VK_NULL_HANDLE;
    VmaAllocation     allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo  = {};
    VkFormat          format     = VK_FORMAT_UNDEFINED;
    VkExtent2D        extent     = {0, 0};
    VkImageLayout     layout     = VK_IMAGE_LAYOUT_UNDEFINED;

    void create(VkContext* context,
                uint32_t width,
                uint32_t height,
                VkFormat format,
                ImageUsage usage,
                VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);
    void cleanup(VkContext* context);

    void transitionLayout(VkCommandBuffer cmdBuffer,
                          VkImageLayout newLayout,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage,
                          VkAccessFlags srcAccess,
                          VkAccessFlags dstAccess);

private:
    VkImageUsageFlags getUsageFlags(ImageUsage usage);
};

#endif // VKIMAGE_H