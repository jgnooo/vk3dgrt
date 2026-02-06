#include "vkcontext.h"
#include "vkerror.h"

#include <GLFW/glfw3.h>

#include <vector>
#include <set>
#include <algorithm>


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

    features2.pNext                   = &syncFeatures;
    syncFeatures.pNext                = &atomicFloatFeatures;
    atomicFloatFeatures.pNext         = &bufferDeviceAddressFeatures;
    bufferDeviceAddressFeatures.pNext = &accelStructFeatures;
    accelStructFeatures.pNext         = &rayTracingPipelineFeatures;
    rayTracingPipelineFeatures.pNext  = &dynamicRenderingFeatures;
    dynamicRenderingFeatures.pNext    = nullptr;

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
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}