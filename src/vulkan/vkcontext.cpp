#include "vkcontext.h"
#include "vkerror.h"

#include <GLFW/glfw3.h>

#include <vector>


void VkContext::initialize(GLFWwindow* window)
{
    createInstance();
}


void VkContext::createInstance()
{
    uint32_t extensionCount = 0;
    const char** extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
    std::vector<const char*> extensions(extensionNames, extensionNames + extensionCount);

    std::vector<const char*> layers;
#ifndef NDEBUG
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    VkApplicationInfo appInfo{
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vkengine",
        .apiVersion       = VK_API_VERSION_1_3
    };

    VkValidationFeatureEnableEXT enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT
    };

    VkValidationFeaturesEXT features = {
        .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures    = enables,
    };

    VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifndef NDEBUG
        .pNext = layers.empty() ? nullptr : &features,
#endif
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames     = layers.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));
}


void VkContext::createSurface(GLFWwindow* window)
{
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
}


void VkContext::cleanup()
{
    vkDestroyInstance(instance, nullptr);
}