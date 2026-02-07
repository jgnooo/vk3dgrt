#ifndef VKBUFFER_H
#define VKBUFFER_H


#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include <cstdint>


struct VkContext;


enum class BufferUsage
{
    VERTEX,
    INDEX,
    UNIFORM,
    STORAGE,
    STAGING,
    ACCELERATION_STRUCTURE,
    SHADER_BINDING_TABLE,
    AS_BUILD_INPUT
};


struct AllocatedBuffer
{
    VkBuffer          buffer        = VK_NULL_HANDLE;
    VmaAllocation     allocation    = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo     = {};
    VkDeviceSize      size          = 0;
    VkDeviceAddress   deviceAddress = 0;

    void create(VkContext* context,
                VkDeviceSize size,
                BufferUsage usage,
                bool hostVisible = false);
    void cleanup(VmaAllocator allocator);

    void  upload(const void* data, VkDeviceSize dataSize, VkDeviceSize offset = 0);
    void* map(VmaAllocator allocator);
    void  unmap(VmaAllocator allocator);

    void* getMappedData() const { return allocInfo.pMappedData; }

private:
    VkBufferUsageFlags getUsageFlags(BufferUsage usage);
};

#endif // VKBUFFER_H