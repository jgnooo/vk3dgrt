#include "vkbuffer.h"
#include "vkcontext.h"
#include "vkerror.h"

#include <stdexcept>
#include <cstring>


VkBufferUsageFlags AllocatedBuffer::getUsageFlags(BufferUsage usage)
{
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    switch (usage)
    {
        case BufferUsage::VERTEX:
            flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            break;
        case BufferUsage::INDEX:
            flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            break;
        case BufferUsage::UNIFORM:
            flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            break;
        case BufferUsage::STORAGE:
            flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case BufferUsage::STAGING:
            flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            break;
        case BufferUsage::ACCELERATION_STRUCTURE:
            flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
            break;
        case BufferUsage::SHADER_BINDING_TABLE:
            flags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case BufferUsage::AS_BUILD_INPUT:
            flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
    }

    return flags;
}


void AllocatedBuffer::create(VkContext* context,
                             VkDeviceSize bufferSize,
                             BufferUsage usage,
                             bool hostVisible)
{
    size = bufferSize;

    VkBufferCreateInfo bufferInfo{
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = getUsageFlags(usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocCreateInfo{};
    if (hostVisible)
    {
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    else
    {
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    VK_CHECK(vmaCreateBuffer(
        context->getAllocator(),
        &bufferInfo,
        &allocCreateInfo,
        &buffer,
        &allocation,
        &allocInfo
    ));

    if (usage != BufferUsage::STAGING)
    {
        VkBufferDeviceAddressInfo addressInfo{
            .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer
        };
        deviceAddress = vkGetBufferDeviceAddressKHR_(context->getDevice(), &addressInfo);
    }
}


void AllocatedBuffer::cleanup(VmaAllocator allocator)
{
    if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer     = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }

    size          = 0;
    deviceAddress = 0;
    allocInfo     = {};
}


void AllocatedBuffer::upload(const void* data, VkDeviceSize dataSize, VkDeviceSize offset)
{
    if (allocInfo.pMappedData == nullptr)
    {
        throw std::runtime_error("[AllocatedBuffer] Buffer is not mapped. Create with hostVisible=true.");
    }

    memcpy(static_cast<char*>(allocInfo.pMappedData) + offset, data, dataSize);
}


void* AllocatedBuffer::map(VmaAllocator allocator)
{
    if (allocInfo.pMappedData == nullptr)
    {
        VK_CHECK(vmaMapMemory(allocator, allocation, &allocInfo.pMappedData));
    }
    return allocInfo.pMappedData;
}


void AllocatedBuffer::unmap(VmaAllocator allocator)
{
    if (allocInfo.pMappedData != nullptr)
    {
        vmaUnmapMemory(allocator, allocation);
        allocInfo.pMappedData = nullptr;
    }
}