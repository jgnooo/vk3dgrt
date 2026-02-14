#include "gui.h"
#include "vulkan/vkengine.h"
#include "vulkan/vkerror.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

#include <stdexcept>


void ImGuiManager::initialize(GLFWwindow* window,
                              VkContext* context,
                              VkSwapchain* swapchain)
{
    if (initialized_)
    {
        throw std::runtime_error("[ImGuiManager] Already initialized.");
    }

    context_ = context;
    swapchain_ = swapchain;

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Initialize GLFW backend
    if (!ImGui_ImplGlfw_InitForVulkan(window, true))
    {
        throw std::runtime_error("[ImGuiManager] Failed to initialize ImGui GLFW backend.");
    }

    // Setup Vulkan backend with dynamic rendering (Vulkan 1.3)
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion          = VK_API_VERSION_1_3;
    initInfo.Instance            = context->instance;
    initInfo.PhysicalDevice      = context->physicalDevice;
    initInfo.Device              = context->device;
    initInfo.QueueFamily         = context->queueFamilyIndices.at(QueueType::GRAPHICS);
    initInfo.Queue               = context->queues.at(QueueType::GRAPHICS);
    initInfo.DescriptorPoolSize  = 16;                                                   // Auto-create descriptor pool
    initInfo.MinImageCount       = 2;
    initInfo.ImageCount          = static_cast<uint32_t>(swapchain->images.size());
    initInfo.UseDynamicRendering = true;

    // Setup pipeline rendering info for dynamic rendering
    VkFormat colorFormat = swapchain->format;
    VkPipelineRenderingCreateInfoKHR pipelineRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat
    };
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("[ImGuiManager] Failed to initialize ImGui Vulkan backend.");
    }

    initialized_ = true;
}


void ImGuiManager::shutdown()
{
    if (initialized_)
    {
        // Wait for GPU to finish before cleanup
        vkDeviceWaitIdle(context_->device);

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        initialized_ = false;
    }
}


void ImGuiManager::newFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}


void ImGuiManager::render()
{
    ImGui::Render();
}


void ImGuiManager::renderDrawData(VkCommandBuffer cmdBuffer)
{
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData)
    {
        ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
    }
}


void ImGuiManager::showFpsOverlay()
{
    if (!showFpsOverlay_)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Set window position to top-left corner with a small padding
    const float kPadding = 10.0f;
    ImGui::SetNextWindowPos(ImVec2(kPadding, kPadding), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##FPS Overlay", &showFpsOverlay_, windowFlags))
    {
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::Text("%.3f ms", 1000.0f / io.Framerate);

        // VRAM usage
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(context_->allocator, budgets);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(context_->physicalDevice, &memProps);

        VkDeviceSize vramUsed  = 0;
        VkDeviceSize vramTotal = 0;
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
        {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                vramUsed  += budgets[i].usage;
                vramTotal += memProps.memoryHeaps[i].size;
            }
        }

        ImGui::Separator();
        ImGui::Text("VRAM: %.0f / %.0f MB",
                    static_cast<float>(vramUsed) / (1024.0f * 1024.0f),
                    static_cast<float>(vramTotal) / (1024.0f * 1024.0f));
    }
    ImGui::End();
}


void ImGuiManager::showRightPanel()
{
    if (!showRightPanel_)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Padding from edges
    const float kPadding = 10.0f;

    // Calculate position: always on the right side with padding
    float panelPosX   = io.DisplaySize.x - rightPanelWidth_ - kPadding;
    float panelPosY   = kPadding;
    float panelHeight = io.DisplaySize.y - (kPadding * 2.0f);

    ImGui::SetNextWindowPos(ImVec2(panelPosX, panelPosY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth_, panelHeight), ImGuiCond_Always);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;

    if (ImGui::Begin("vk3dgrt", &showRightPanel_, windowFlags))
    {
        // Render Mode Section
        ImGui::Text("Render Mode");
        ImGui::Separator();
        ImGui::Spacing();

        // Radio buttons in a single row (centered)
        int mode = static_cast<int>(renderMode_);

        // Calculate total width of radio buttons
        float radioWidth1 = ImGui::CalcTextSize("GS").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        float radioWidth2 = ImGui::CalcTextSize("Point").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        float radioWidth3 = ImGui::CalcTextSize("Splat").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        float spacing     = 20.0f * 2.0f;  // Custom spacing between buttons
        float totalWidth  = radioWidth1 + radioWidth2 + radioWidth3 + spacing;

        // Center the radio buttons
        float availWidth = ImGui::GetContentRegionAvail().x;
        float offsetX    = (availWidth - totalWidth) * 0.5f;
        if (offsetX > 0.0f)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        }

        if (ImGui::RadioButton("GS", &mode, GUI_RENDER_MODE_GS))
        {
            renderModeChanged_ = true;
        }
        ImGui::SameLine(0.0f, 20.0f);
        if (ImGui::RadioButton("Point", &mode, GUI_RENDER_MODE_POINT))
        {
            renderModeChanged_ = true;
        }
        ImGui::SameLine(0.0f, 20.0f);
        if (ImGui::RadioButton("Splat", &mode, GUI_RENDER_MODE_SPLAT))
        {
            renderModeChanged_ = true;
        }

        renderMode_ = static_cast<uint32_t>(mode);

        // SH Degree Section
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("SH Degree");
        ImGui::Separator();
        ImGui::Spacing();

        if (!shAvailable_)
        {
            ImGui::BeginDisabled();
        }

        int degree = shDegree_;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt("##SHDegree", &degree, 0, 3))
        {
            shDegreeChanged_ = true;
        }
        shDegree_ = degree;

        if (!shAvailable_)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("No SH data in scene");
        }
    }
    ImGui::End();
}