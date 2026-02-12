#include "vkcontext.h"
#include "vkerror.h"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <vector>
#include <set>
#include <algorithm>
#include <cstring>
#include <cstdio>


// --------------------------------------------------- //
//  Extension Function Pointers
// --------------------------------------------------- //
PFN_vkGetBufferDeviceAddressKHR                vkGetBufferDeviceAddressKHR_                = nullptr;
PFN_vkCreateAccelerationStructureKHR           vkCreateAccelerationStructureKHR_           = nullptr;
PFN_vkDestroyAccelerationStructureKHR          vkDestroyAccelerationStructureKHR_          = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR    vkGetAccelerationStructureBuildSizesKHR_    = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_ = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR        vkCmdBuildAccelerationStructuresKHR_        = nullptr;
PFN_vkCreateRayTracingPipelinesKHR             vkCreateRayTracingPipelinesKHR_             = nullptr;
PFN_vkGetRayTracingShaderGroupHandlesKHR       vkGetRayTracingShaderGroupHandlesKHR_       = nullptr;
PFN_vkCmdTraceRaysKHR                          vkCmdTraceRaysKHR_                          = nullptr;


// --------------------------------------------------- //
//  Helper Functions
// --------------------------------------------------- //

static void printGpuInfo(uint32_t order, VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

    const char* typeStr = "Other";
    switch (props.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeStr = "Discrete GPU";   break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeStr = "Virtual GPU";    break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeStr = "CPU";            break;
        default: break;
    }

    uint32_t apiMaj   = VK_VERSION_MAJOR(props.apiVersion);
    uint32_t apiMin   = VK_VERSION_MINOR(props.apiVersion);
    uint32_t apiPatch = VK_VERSION_PATCH(props.apiVersion);

    uint32_t drvMaj   = VK_VERSION_MAJOR(props.driverVersion);
    uint32_t drvMin   = VK_VERSION_MINOR(props.driverVersion);
    uint32_t drvPatch = VK_VERSION_PATCH(props.driverVersion);

    printf(" +-[ GPU %u ]--------------------------------------------------+\n", order);
    printf(" | Device Name    : %-41s \n", props.deviceName);
    printf(" | Device Type    : %-41s \n", typeStr);
    printf(" | API Version    : %u.%u.%-37u \n", apiMaj, apiMin, apiPatch);
    printf(" | Driver Version : %u.%u.%-37u \n", drvMaj, drvMin, drvPatch);
    printf(" +------------------------------------------------------------+\n");
}


static bool deviceSupportsExtensions(VkPhysicalDevice physicalDevice,
                                     const std::vector<const char*>& extensions)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, deviceExtensions.data());

    return std::all_of(extensions.begin(), extensions.end(), [&](const char* reqExtension)
    {
        return std::any_of(deviceExtensions.begin(), deviceExtensions.end(), [&](const auto& props)
        {
            return strcmp(props.extensionName, reqExtension) == 0;
        });
    });
}


// --------------------------------------------------- //
//  VkContext Implementation
// --------------------------------------------------- //

void VkContext::initialize(GLFWwindow* window)
{
    createInstance();
    createSurface(window);
    createDevice();
    createAllocator();
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


void VkContext::createDevice()
{
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    if (deviceCount == 0)
        throw std::runtime_error("[VkContext] No Vulkan physical devices found.");
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()));

    printf("\n");
    printf(" =============================================================\n");
    printf("  Detecting Physical Devices... (%u found)\n", deviceCount);
    printf(" =============================================================\n");

    for (uint32_t i = 0; i < deviceCount; ++i)
        printGpuInfo(i, physicalDevices[i]);
    printf("\n");

    std::vector<const char*> requiredExtensions = {
        VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
    };

    queueFamilyIndices.clear();
    queueFamilyIndices[QueueType::GRAPHICS] = static_cast<uint32_t>(-1);
    queueFamilyIndices[QueueType::COMPUTE]  = static_cast<uint32_t>(-1);
    queueFamilyIndices[QueueType::TRANSFER] = static_cast<uint32_t>(-1);

    // NOTE: Look for a discrete (external) GPU
    VkPhysicalDeviceProperties props;
    for (const auto& pDevice : physicalDevices)
    {
        vkGetPhysicalDeviceProperties(pDevice, &props);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

        if (!deviceSupportsExtensions(pDevice, requiredExtensions))
            continue;

        uint32_t graphicsIdx = static_cast<uint32_t>(-1);
        uint32_t computeIdx  = static_cast<uint32_t>(-1);
        uint32_t transferIdx = static_cast<uint32_t>(-1);

        for (uint32_t i = 0; i < queueFamilies.size(); ++i)
        {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                glfwGetPhysicalDevicePresentationSupport(instance, pDevice, i))
            {
                graphicsIdx = i;
                break;
            }
        }

        for (uint32_t i = 0; i < queueFamilies.size(); ++i)
        {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                computeIdx = i;
                break;
            }
        }

        if (computeIdx == static_cast<uint32_t>(-1))
        {
            for (uint32_t i = 0; i < queueFamilies.size(); ++i)
            {
                if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
                {
                    computeIdx = i;
                    break;
                }
            }
        }

        for (uint32_t i = 0; i < queueFamilies.size(); ++i)
        {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
            {
                transferIdx = i;
                break;
            }
        }

        if (transferIdx == static_cast<uint32_t>(-1))
        {
            for (uint32_t i = 0; i < queueFamilies.size(); ++i)
            {
                if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
                {
                    transferIdx = i;
                    break;
                }
            }
        }

        if (graphicsIdx != static_cast<uint32_t>(-1) &&
            computeIdx != static_cast<uint32_t>(-1) &&
            transferIdx != static_cast<uint32_t>(-1))
        {
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                physicalDevice                        = pDevice;
                queueFamilyIndices[QueueType::GRAPHICS] = graphicsIdx;
                queueFamilyIndices[QueueType::COMPUTE]  = computeIdx;
                queueFamilyIndices[QueueType::TRANSFER] = transferIdx;
                break;
            }
        }
    }

    if (physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("[VkContext] No suitable Vulkan physical device found.");

    printf(" >> Selected GPU: [%s]\n\n", props.deviceName);
    fflush(stdout);

    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilyIndices[QueueType::GRAPHICS],
        queueFamilyIndices[QueueType::COMPUTE],
        queueFamilyIndices[QueueType::TRANSFER]
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 0.5f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamily,
            .queueCount       = 1,
            .pQueuePriorities = &queuePriority
        };
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures2 features2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };

    VkPhysicalDeviceSynchronization2Features syncFeatures{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .synchronization2 = VK_TRUE
    };

    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures{
        .sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
        .shaderBufferFloat32AtomicAdd = VK_TRUE
    };

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{
        .sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .bufferDeviceAddress = VK_TRUE
    };

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure = VK_TRUE
    };

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .rayTracingPipeline = VK_TRUE
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE
    };

    VkPhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayoutFeatures{
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
        .scalarBlockLayout = VK_TRUE
    };

    features2.pNext                   = &syncFeatures;
    syncFeatures.pNext                = &atomicFloatFeatures;
    atomicFloatFeatures.pNext         = &bufferDeviceAddressFeatures;
    bufferDeviceAddressFeatures.pNext = &accelStructFeatures;
    accelStructFeatures.pNext         = &rayTracingPipelineFeatures;
    rayTracingPipelineFeatures.pNext  = &dynamicRenderingFeatures;
    dynamicRenderingFeatures.pNext    = &scalarBlockLayoutFeatures;
    scalarBlockLayoutFeatures.pNext   = nullptr;

    VkDeviceCreateInfo deviceCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &features2,
        .queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos       = queueCreateInfos.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data()
    };

    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

    for (const auto& [type, index] : queueFamilyIndices)
    {
        VkQueue vkQueue;
        vkGetDeviceQueue(device, index, 0, &vkQueue);
        queues[type] = vkQueue;
    }

    // Load extension function pointers
    vkGetBufferDeviceAddressKHR_                   = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR"));
    vkCreateAccelerationStructureKHR_              = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    vkDestroyAccelerationStructureKHR_             = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    vkGetAccelerationStructureBuildSizesKHR_       = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    vkGetAccelerationStructureDeviceAddressKHR_    = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
    vkCmdBuildAccelerationStructuresKHR_           = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
    vkCreateRayTracingPipelinesKHR_                = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
    vkGetRayTracingShaderGroupHandlesKHR_          = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
    vkCmdTraceRaysKHR_                             = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));
    vkCmdWriteAccelerationStructuresPropertiesKHR_ = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(vkGetDeviceProcAddr(device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
    vkCmdCopyAccelerationStructureKHR_             = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkCmdCopyAccelerationStructureKHR"));

    // Get ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProps{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
    };
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProps{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
        .pNext = &rayTracingPipelineProps,
    };
    VkPhysicalDeviceProperties2 deviceProps2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &accelStructProps,
    };

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProps2);

    rtProps.shaderGroupHandleAlignment                     = rayTracingPipelineProps.shaderGroupHandleAlignment;
    rtProps.shaderGroupBaseAlignment                       = rayTracingPipelineProps.shaderGroupBaseAlignment;
    rtProps.minAccelerationStructureScratchOffsetAlignment = accelStructProps.minAccelerationStructureScratchOffsetAlignment;
}


void VkContext::createAllocator()
{
    VmaVulkanFunctions vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr   = vkGetDeviceProcAddr
    };

    VmaAllocatorCreateInfo allocatorInfo{
        .flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
                            VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT,
        .physicalDevice   = physicalDevice,
        .device           = device,
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3
    };

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

    printf(" >> VMA Allocator created successfully.\n");
}


void VkContext::cleanup()
{
    vkDeviceWaitIdle(device);
    if (allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}


// --------------------------------------------------- //
//  VkSwapchain Implementation
// --------------------------------------------------- //

void VkSwapchain::create(VkContext* context, uint32_t width, uint32_t height)
{
    VkSurfaceKHR surface = context->getSurface();
    VkPhysicalDevice physicalDevice = context->getPhysicalDevice();

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        extent = capabilities.currentExtent;
    }
    else
    {
        extent.width  = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR selectedFormat = formats[0];
    for (const auto& fmt : formats)
    {
        if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            selectedFormat = fmt;
            break;
        }
    }
    VkFormat swapchainFormat = selectedFormat.format;

    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, swapchainFormat, &formatProps);
    if ((imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        !(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
        throw std::runtime_error("[VkSwapchain] Selected swapchain format does not support color attachment.");

    if ((imageUsage & VK_IMAGE_USAGE_STORAGE_BIT) &&
        !(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("[VkSwapchain] Selected swapchain format does not support storage image.");

    if ((imageUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
        !(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
        throw std::runtime_error("[VkSwapchain] Selected swapchain format does not support transfer dst.");

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    for (const auto& pm : presentModes)
    {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentMode = pm;
            break;
        }
    }

    uint32_t minSwapChainImages = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && minSwapChainImages > capabilities.maxImageCount)
        minSwapChainImages = capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = minSwapChainImages,
        .imageFormat      = swapchainFormat,
        .imageColorSpace  = selectedFormat.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = imageUsage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE
    };

    VkDevice device = context->getDevice();

    VK_CHECK(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain));

    format = swapchainFormat;

    vkGetSwapchainImagesKHR(device, swapchain, &minSwapChainImages, nullptr);
    images.resize(minSwapChainImages);
    vkGetSwapchainImagesKHR(device, swapchain, &minSwapChainImages, images.data());

    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{
            .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image      = images[i],
            .viewType   = VK_IMAGE_VIEW_TYPE_2D,
            .format     = swapchainFormat,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            }
        };
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]));
    }
}


void VkSwapchain::cleanup(VkDevice device)
{
    for (auto imageView : imageViews)
        vkDestroyImageView(device, imageView, nullptr);

    imageViews.clear();
    images.clear();

    vkDestroySwapchainKHR(device, swapchain, nullptr);
}