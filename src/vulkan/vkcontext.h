#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <vulkan/vulkan_core.h>


struct GLFWwindow;


struct VkContext
{
    VkInstance instance = VK_NULL_HANDLE;

    void initialize(GLFWwindow* window);
    void createInstance();
    void cleanup();
};

#endif // VKCONTEXT_H