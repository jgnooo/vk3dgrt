#ifndef VKPROVIDER_H
#define VKPROVIDER_H

#include "vkcontext.h"


class VkProvider
{
public:
    virtual VkContext& getContext() = 0;
    
    virtual ~VkProvider() = default;
};


#endif // VKPROVIDER_H
