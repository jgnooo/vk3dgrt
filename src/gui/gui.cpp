#include "gui.h"
#include "vulkan/vkengine.h"
#include "vulkan/vkerror.h"
#include "3dgrt/grt-scene.h"

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


void ImGuiManager::showAxisGizmo(const glm::mat4& viewMatrix)
{
    axisGizmo_.draw(viewMatrix);
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

    if (ImGui::Begin("vk3dgrt", nullptr, windowFlags))
    {
        // ── Assets Section ──
        if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Mesh Models
            if (ImGui::TreeNode("Mesh Models"))
            {
                // Teapot: [Load] button + per-instance [X] remove buttons
                uint32_t meshCount = scene_ ? scene_->getMeshCount() : 0;

                float availWidth    = ImGui::GetContentRegionAvail().x;
                float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
                float removeWidth   = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;

                if (meshCount == 0)
                {
                    // No meshes — show load button
                    if (ImGui::Button("teapot.obj", ImVec2(-1.0f, 0.0f)))
                    {
                        insertTeapot_ = true;
                    }
                }
                else
                {
                    // Show each inserted mesh with a remove button
                    const auto& meshes = scene_->getMeshInstances();
                    for (uint32_t i = 0; i < meshCount; ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));

                        // Mesh name label
                        float labelWidth = availWidth - removeWidth - buttonSpacing;
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ImGui::Button(meshes[i].name.c_str(), ImVec2(labelWidth, 0.0f));
                        ImGui::PopStyleColor();
                        
                        // Remove button
                        ImGui::SameLine(0.0f, buttonSpacing);
                        if (ImGui::Button("X", ImVec2(removeWidth, 0.0f)))
                        {
                            removeMeshIndex_ = static_cast<int>(i);
                        }

                        ImGui::PopID();

                        ImGui::Spacing();
                    }

                    // Add more button
                    if (ImGui::Button("+ teapot.obj", ImVec2(-1.0f, 0.0f)))
                    {
                        insertTeapot_ = true;
                    }
                }

                ImGui::TreePop();
            }
        }

        ImGui::Spacing();

        // ── Renderer Section ──
        if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Camera
            ImGui::Text("Camera");
            ImGui::Spacing();

            int camType = static_cast<int>(cameraType_);

            {
                const char* labels[]  = {"Pinhole", "Fisheye"};
                const int   values[]  = {GUI_CAMERA_PINHOLE, GUI_CAMERA_FISHEYE};
                constexpr int kCount  = 2;

                float radioHeight  = ImGui::GetFrameHeight();
                float innerPadding = ImGui::GetStyle().ItemInnerSpacing.x;

                float totalRadioWidth = 0.0f;
                for (int i = 0; i < kCount; ++i)
                {
                    totalRadioWidth += radioHeight + innerPadding + ImGui::CalcTextSize(labels[i]).x;
                }

                float availWidth  = ImGui::GetContentRegionAvail().x;
                float usableWidth = availWidth * 0.92f;
                float gapBetween  = usableWidth - totalRadioWidth;
                float offsetX     = (availWidth - usableWidth) * 0.5f;

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

                for (int i = 0; i < kCount; ++i)
                {
                    if (i > 0)
                    {
                        ImGui::SameLine(0.0f, gapBetween);
                    }
                    if (ImGui::RadioButton(labels[i], &camType, values[i]))
                    {
                        cameraTypeChanged_ = true;
                    }
                }
            }

            cameraType_ = static_cast<uint32_t>(camType);

            // Render Mode
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Render Mode");
            ImGui::Spacing();

            int mode = static_cast<int>(renderMode_);

            {
                const char* labels[]   = {"GS", "Point", "Splat"};
                const int   values[]   = {GUI_RENDER_MODE_GS, GUI_RENDER_MODE_POINT, GUI_RENDER_MODE_SPLAT};
                constexpr int kCount   = 3;

                float radioHeight  = ImGui::GetFrameHeight();
                float innerPadding = ImGui::GetStyle().ItemInnerSpacing.x;

                float totalRadioWidth = 0.0f;
                for (int i = 0; i < kCount; ++i)
                {
                    totalRadioWidth += radioHeight + innerPadding + ImGui::CalcTextSize(labels[i]).x;
                }

                float availWidth  = ImGui::GetContentRegionAvail().x;
                float usableWidth = availWidth * 0.92f;
                float totalGap    = usableWidth - totalRadioWidth;
                float gapBetween  = totalGap / static_cast<float>(kCount - 1);
                float offsetX     = (availWidth - usableWidth) * 0.5f;

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

                for (int i = 0; i < kCount; ++i)
                {
                    if (i > 0)
                    {
                        ImGui::SameLine(0.0f, gapBetween);
                    }
                    if (ImGui::RadioButton(labels[i], &mode, values[i]))
                    {
                        renderModeChanged_ = true;
                    }
                }
            }

            renderMode_ = static_cast<uint32_t>(mode);

            // SH Degree - segmented button style
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("SH Degree");
            ImGui::Spacing();

            if (!shAvailable_)
            {
                ImGui::BeginDisabled();
            }

            {
                int degree          = shDegree_;
                float availWidth    = ImGui::GetContentRegionAvail().x;
                float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
                float buttonWidth   = (availWidth - buttonSpacing * 3.0f) / 4.0f;

                for (int i = 0; i <= 3; ++i)
                {
                    if (i > 0)
                    {
                        ImGui::SameLine(0.0f, buttonSpacing);
                    }

                    bool isSelected = (degree == i);

                    if (isSelected)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    }

                    char label[8];
                    snprintf(label, sizeof(label), "%d", i);

                    if (ImGui::Button(label, ImVec2(buttonWidth, 0.0f)))
                    {
                        degree           = i;
                        shDegreeChanged_ = true;
                    }

                    if (isSelected)
                    {
                        ImGui::PopStyleColor();
                    }
                }

                shDegree_ = degree;
            }

            if (!shAvailable_)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("No SH data in scene");
            }

            // Reflection
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Reflection");
            ImGui::Spacing();

            if (ImGui::Checkbox("Enable##Reflection", &reflectEnabled_))
            {
                reflectEnabledChanged_ = true;
            }

            if (reflectEnabled_)
            {
                ImGui::Spacing();
                ImGui::SliderInt("Max Bounces", &maxBounces_, 1, 3);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    maxBouncesChanged_ = true;
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Fisheye");
        ImGui::Spacing();

        if (ImGui::TreeNode("Fisheye Parameters"))
        {
            bool isFisheye = (cameraType_ == GUI_CAMERA_FISHEYE);
            if (!isFisheye)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::SliderAngle("FOV", &fisheyeFovDeg_, 10.0f, 360.0f))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::SliderAngle("Max Angle", &fisheyeMaxAngleDeg_, 5.0f, 180.0f))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::DragFloat("Center X", &fisheyeCx_, 0.5f, -500.0f, 500.0f, "%.1f px"))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::DragFloat("Center Y", &fisheyeCy_, 0.5f, -500.0f, 500.0f, "%.1f px"))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::DragFloat("k1", &fisheyeK1_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::DragFloat("k2", &fisheyeK2_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::DragFloat("k3", &fisheyeK3_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
            }

            if (ImGui::DragFloat("k4", &fisheyeK4_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
            }

            if (!isFisheye)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Switch to Fisheye camera to adjust");
            }

            ImGui::TreePop();
        }
    }
    ImGui::End();
}