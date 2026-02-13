#ifndef VKPROVIDER_H
#define VKPROVIDER_H

#include "vkcontext.h"


class VkProvider
{
public:
    virtual VkContext&      getContext()                                               = 0;
    virtual VkCommandPool   getCommandPool(QueueType type)                             = 0;
    virtual VkQueue         getQueue(QueueType type)                                   = 0;
    virtual VkSwapchain&    getSwapchain()                                             = 0;
    virtual VkExtent2D      getSwapchainExtent()                                       = 0;
    virtual VkFormat        getSwapchainFormat()                                       = 0;
    virtual VkCommandBuffer beginSingleTimeCommands(QueueType type)                    = 0;
    virtual void            endSingleTimeCommands(VkCommandBuffer cmd, QueueType type) = 0;
    
    virtual ~VkProvider() = default;
};


#endif // VKPROVIDER_H
