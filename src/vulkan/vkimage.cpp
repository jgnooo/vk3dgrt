#include "vkimage.h"
#include "vkcontext.h"
#include "vkerror.h"

#include <stdexcept>


VkImageUsageFlags AllocatedImage::getUsageFlags(ImageUsage usage)
{
    switch (usage)
    {
        case ImageUsage::COLOR_ATTACHMENT:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        case ImageUsage::DEPTH_ATTACHMENT:
            return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        case ImageUsage::STORAGE:
            return VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        case ImageUsage::SAMPLED:
            return VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        case ImageUsage::TRANSFER_SRC:
            return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        case ImageUsage::TRANSFER_DST:
            return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        default:
            return VK_IMAGE_USAGE_SAMPLED_BIT;
    }
}


void AllocatedImage::create(VkContext* context,
                            uint32_t width,
                            uint32_t height,
                            VkFormat imageFormat,
                            ImageUsage usage,
                            VkImageAspectFlags aspectFlags)
{
    format = imageFormat;
    extent = {width, height};

    VkImageCreateInfo imageInfo{
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = {width, height, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = getUsageFlags(usage),
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocCreateInfo{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    VK_CHECK(vmaCreateImage(
        context->getAllocator(),
        &imageInfo,
        &allocCreateInfo,
        &image,
        &allocation,
        &allocInfo
    ));

    VkImageViewCreateInfo viewInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = format,
        .components       = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {
            .aspectMask     = aspectFlags,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    VK_CHECK(vkCreateImageView(context->getDevice(), &viewInfo, nullptr, &imageView));

    layout = VK_IMAGE_LAYOUT_UNDEFINED;
}


void AllocatedImage::cleanup(VkContext* context)
{
    if (imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(context->getDevice(), imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }

    if (image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE)
    {
        vmaDestroyImage(context->getAllocator(), image, allocation);
        image      = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }

    format    = VK_FORMAT_UNDEFINED;
    extent    = {0, 0};
    layout    = VK_IMAGE_LAYOUT_UNDEFINED;
    allocInfo = {};
}


void AllocatedImage::transitionLayout(VkCommandBuffer cmdBuffer,
                                      VkImageLayout newLayout,
                                      VkPipelineStageFlags srcStage,
                                      VkPipelineStageFlags dstStage,
                                      VkAccessFlags srcAccess,
                                      VkAccessFlags dstAccess)
{
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT)
    {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == VK_FORMAT_D24_UNORM_S8_UINT)
        {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    VkImageMemoryBarrier barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = srcAccess,
        .dstAccessMask       = dstAccess,
        .oldLayout           = layout,
        .newLayout           = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = aspectMask,
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

    layout = newLayout;
}