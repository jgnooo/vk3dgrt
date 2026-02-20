#include "gui.h"
#include "vulkan/vkengine.h"
#include "vulkan/vkerror.h"
#include "3dgrt/grt-scene.h"
#include "3dgrt/mesh-data.h"
#include "log.h"

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
        fpsOverlayHeight_ = 0.0f;
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

    fpsOverlayHeight_ = ImGui::GetWindowHeight();
    ImGui::End();
}


void ImGuiManager::showAxisGizmo(const glm::mat4& viewMatrix)
{
    axisGizmo_.draw(viewMatrix);
}


void ImGuiManager::setSelectedMeshIndex(int index)
{
    selectedMeshIndex_ = index;

    if (index >= 0 && scene_)
    {
        const auto& meshes = scene_->getMeshInstances();
        if (index < static_cast<int>(meshes.size()))
        {
            editTransform_ = meshes[index].meshTransform;
        }
    }
}


void ImGuiManager::showTransformPanel()
{
    if (selectedMeshIndex_ < 0 || !scene_)
    {
        return;
    }

    const auto& meshes = scene_->getMeshInstances();
    if (selectedMeshIndex_ >= static_cast<int>(meshes.size()))
    {
        selectedMeshIndex_ = -1;
        return;
    }

    const float kPadding = 10.0f;
    float panelY = kPadding + fpsOverlayHeight_ + kPadding;

    ImGui::SetNextWindowPos(ImVec2(kPadding, panelY), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 0.0f), ImVec2(250.0f, 600.0f));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##Transform", nullptr, flags))
    {
        const auto& mesh = meshes[selectedMeshIndex_];

        // Mesh name
        ImGui::Text("%s", mesh.name.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        // Gizmo mode selection buttons
        float availWidth    = ImGui::GetContentRegionAvail().x;
        float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
        float buttonWidth   = (availWidth - buttonSpacing * 2.0f) / 3.0f;

        GizmoMode mode = meshGizmo_.getMode();

        // Translate button
        bool isTrans = (mode == GizmoMode::TRANSLATE);
        if (isTrans)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("T##mode", ImVec2(buttonWidth, 0.0f)))
        {
            meshGizmo_.setMode(GizmoMode::TRANSLATE);
            Log::INFO("GUI") << "Gizmo mode changed: Translate";
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Translate (T)");
        }
        if (isTrans)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine(0.0f, buttonSpacing);

        // Rotate button
        bool isRot = (mode == GizmoMode::ROTATE);
        if (isRot)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("R##mode", ImVec2(buttonWidth, 0.0f)))
        {
            meshGizmo_.setMode(GizmoMode::ROTATE);
            Log::INFO("GUI") << "Gizmo mode changed: Rotate";
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Rotate (R)");
        }
        if (isRot)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine(0.0f, buttonSpacing);

        // Scale button
        bool isScl = (mode == GizmoMode::SCALE);
        if (isScl)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("S##mode", ImVec2(buttonWidth, 0.0f)))
        {
            meshGizmo_.setMode(GizmoMode::SCALE);
            Log::INFO("GUI") << "Gizmo mode changed: Scale";
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Scale (S)");
        }
        if (isScl)
        {
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Position
        ImGui::Text("Position");
        ImGui::PushItemWidth(-1.0f);
        bool posChanged = false;
        posChanged |= ImGui::DragFloat("X##pos", &editTransform_.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
        posChanged |= ImGui::DragFloat("Y##pos", &editTransform_.position.y, 0.1f, 0.0f, 0.0f, "%.2f");
        posChanged |= ImGui::DragFloat("Z##pos", &editTransform_.position.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        // Rotation (degrees)
        ImGui::Text("Rotation");
        ImGui::PushItemWidth(-1.0f);
        bool rotChanged = false;
        rotChanged |= ImGui::DragFloat("X##rot", &editTransform_.rotation.x, 1.0f, -360.0f, 360.0f, "%.1f");
        rotChanged |= ImGui::DragFloat("Y##rot", &editTransform_.rotation.y, 1.0f, -360.0f, 360.0f, "%.1f");
        rotChanged |= ImGui::DragFloat("Z##rot", &editTransform_.rotation.z, 1.0f, -360.0f, 360.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        // Scale
        ImGui::Text("Scale");
        ImGui::PushItemWidth(-1.0f);
        bool sclChanged = false;
        sclChanged |= ImGui::DragFloat("X##scl", &editTransform_.scale.x, 0.01f, 0.01f, 100.0f, "%.3f");
        sclChanged |= ImGui::DragFloat("Y##scl", &editTransform_.scale.y, 0.01f, 0.01f, 100.0f, "%.3f");
        sclChanged |= ImGui::DragFloat("Z##scl", &editTransform_.scale.z, 0.01f, 0.01f, 100.0f, "%.3f");
        ImGui::PopItemWidth();

        if (posChanged || rotChanged || sclChanged)
        {
            meshTransformChanged_ = true;
            if (posChanged)
            {
                Log::INFO("GUI") << "Transform position: ("
                                 << editTransform_.position.x << ", "
                                 << editTransform_.position.y << ", "
                                 << editTransform_.position.z << ")";
            }
            if (rotChanged)
            {
                Log::INFO("GUI") << "Transform rotation: ("
                                 << editTransform_.rotation.x << ", "
                                 << editTransform_.rotation.y << ", "
                                 << editTransform_.rotation.z << ")";
            }
            if (sclChanged)
            {
                Log::INFO("GUI") << "Transform scale: ("
                                 << editTransform_.scale.x << ", "
                                 << editTransform_.scale.y << ", "
                                 << editTransform_.scale.z << ")";
            }
        }

        // Keyboard shortcuts for gizmo mode (only when not typing in a text input)
        if (!ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_T))
            {
                meshGizmo_.setMode(GizmoMode::TRANSLATE);
                Log::INFO("GUI") << "Gizmo mode changed (key): Translate";
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_R))
            {
                meshGizmo_.setMode(GizmoMode::ROTATE);
                Log::INFO("GUI") << "Gizmo mode changed (key): Rotate";
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_S))
            {
                meshGizmo_.setMode(GizmoMode::SCALE);
                Log::INFO("GUI") << "Gizmo mode changed (key): Scale";
            }
        }
    }
    ImGui::End();

    // Escape to deselect
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput)
    {
        Log::INFO("GUI") << "Deselect mesh (Escape)";
        selectedMeshIndex_ = -1;
    }
}


void ImGuiManager::updateMeshGizmo(const glm::mat4& viewMatrix,
                                    const glm::mat4& projMatrix,
                                    const glm::vec2& displaySize)
{
    if (selectedMeshIndex_ < 0 || !scene_)
    {
        return;
    }

    const auto& meshes = scene_->getMeshInstances();
    if (selectedMeshIndex_ >= static_cast<int>(meshes.size()))
    {
        return;
    }

    glm::vec3 cameraPos = glm::vec3(glm::inverse(viewMatrix)[3]);

    // Compute gizmo center from editTransform_ + localCenter to stay in sync during drag
    // (using editTransform_ avoids 1-frame lag vs mesh instance's stored transform)
    const auto& selectedMesh = meshes[selectedMeshIndex_];
    glm::vec3 gizmoCenter = glm::vec3(
        editTransform_.toMatrix() * glm::vec4(selectedMesh.localCenter, 1.0f));

    // Handle input first (before drawing, so hover state is current)
    glm::vec3 pos = editTransform_.position;
    glm::vec3 rot = editTransform_.rotation;
    glm::vec3 scl = editTransform_.scale;

    bool modified = meshGizmo_.handleInput(viewMatrix, projMatrix,
                                            cameraPos, gizmoCenter,
                                            displaySize,
                                            pos, rot, scl);

    if (modified)
    {
        editTransform_.position = pos;
        editTransform_.rotation = rot;
        editTransform_.scale    = scl;
        meshTransformChanged_   = true;
    }

    // Block camera input when gizmo is active
    if (meshGizmo_.isDragging() || meshGizmo_.isHovered())
    {
        ImGui::GetIO().WantCaptureMouse = true;
    }

    // Draw the gizmo overlay at the mesh's visual center
    meshGizmo_.draw(viewMatrix, projMatrix, gizmoCenter, displaySize);
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
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("vk3dgrt", nullptr, windowFlags))
    {
        // ── Assets Section ──
        if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::TreeNode("Load Mesh"))
            {
                if (ImGui::Button("+ Plane", ImVec2(-1.0f, 0.0f)))
                {
                    insertPlane_ = true;
                    Log::INFO("GUI") << "Insert mesh: Plane";
                }

                if (ImGui::Button("+ Sphere", ImVec2(-1.0f, 0.0f)))
                {
                    insertSphere_ = true;
                    Log::INFO("GUI") << "Insert mesh: Sphere";
                }

                if (ImGui::Button("+ Teapot", ImVec2(-1.0f, 0.0f)))
                {
                    insertTeapot_ = true;
                    Log::INFO("GUI") << "Insert mesh: Teapot";
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
                        Log::INFO("GUI") << "Camera type changed: " << labels[i];
                    }
                }
            }

            cameraType_ = static_cast<uint32_t>(camType);

            // Visualize Mode
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Visualize Mode");
            ImGui::Spacing();

            int mode = static_cast<int>(visualizeMode_);

            {
                const char* labels[]   = {"GS", "Point", "Splat"};
                const int   values[]   = {GUI_VISUALIZE_MODE_GS, GUI_VISUALIZE_MODE_POINT, GUI_VISUALIZE_MODE_SPLAT};
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
                        visualizeModeChanged_ = true;
                        Log::INFO("GUI") << "Visualize mode changed: " << labels[i];
                    }
                }
            }

            visualizeMode_ = static_cast<uint32_t>(mode);

            // Render Mode
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Render Mode");
            ImGui::Spacing();

            {
                const char* renderModeItems[] = {"Color", "Depth"};
                int currentRenderMode = static_cast<int>(renderMode_);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##RenderMode", &currentRenderMode, renderModeItems, IM_ARRAYSIZE(renderModeItems)))
                {
                    renderModeChanged_ = true;
                    Log::INFO("GUI") << "Render mode changed: " << renderModeItems[currentRenderMode];
                }
                renderMode_ = static_cast<uint32_t>(currentRenderMode);
            }

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
                        Log::INFO("GUI") << "SH degree changed: " << i;
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

            // Meshes
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Meshes");
            ImGui::Spacing();

            {
                uint32_t meshCount = scene_ ? scene_->getMeshCount() : 0;

                if (meshCount == 0)
                {
                    ImGui::TextDisabled("No meshes (add from Assets)");
                }
                else
                {
                    const auto& meshes = scene_->getMeshInstances();

                    float availWidth    = ImGui::GetContentRegionAvail().x;
                    float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
                    const char* materialLabels[] = {"Diffuse", "Reflective"};

                    float removeWidth = ImGui::CalcTextSize("X").x
                                      + ImGui::GetStyle().FramePadding.x * 2.0f;
                    float comboWidth  = availWidth * 0.38f;
                    float nameWidth   = availWidth - comboWidth - removeWidth
                                      - buttonSpacing * 2.0f;

                    for (uint32_t i = 0; i < meshCount; ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));

                        int matType = static_cast<int>(meshes[i].material.type);

                        // Mesh name button (clickable for selection)
                        bool isSelected = (selectedMeshIndex_ == static_cast<int>(i));
                        if (isSelected)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                                  ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
                        }

                        if (ImGui::Button(meshes[i].name.c_str(), ImVec2(nameWidth, 0.0f)))
                        {
                            if (isSelected)
                            {
                                Log::INFO("GUI") << "Deselect mesh: " << meshes[i].name;
                                setSelectedMeshIndex(-1);
                            }
                            else
                            {
                                Log::INFO("GUI") << "Select mesh: " << meshes[i].name << " (index=" << i << ")";
                                setSelectedMeshIndex(static_cast<int>(i));
                            }
                        }
                        ImGui::PopStyleColor();

                        // Material type combo
                        ImGui::SameLine(0.0f, buttonSpacing);
                        ImGui::SetNextItemWidth(comboWidth);
                        if (ImGui::Combo("##mat", &matType, materialLabels, 2))
                        {
                            meshMaterialChangeIdx_ = static_cast<int>(i);
                            meshMaterialNewType_   = matType;
                            Log::INFO("GUI") << "Material changed: " << meshes[i].name
                                             << " -> " << materialLabels[matType];
                        }

                        // Delete button
                        ImGui::SameLine(0.0f, buttonSpacing);
                        if (ImGui::Button("X", ImVec2(removeWidth, 0.0f)))
                        {
                            Log::INFO("GUI") << "Remove mesh: " << meshes[i].name << " (index=" << i << ")";
                            removeMeshIndex_ = static_cast<int>(i);

                            // Handle selection state on deletion
                            if (selectedMeshIndex_ == static_cast<int>(i))
                            {
                                selectedMeshIndex_ = -1;
                            }
                            else if (selectedMeshIndex_ > static_cast<int>(i))
                            {
                                selectedMeshIndex_--;
                            }
                        }

                        ImGui::PopID();
                        ImGui::Spacing();
                    }
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
                Log::INFO("GUI") << "Fisheye FOV changed: " << glm::degrees(fisheyeFovDeg_) << " deg";
            }

            if (ImGui::SliderAngle("Max Angle", &fisheyeMaxAngleDeg_, 5.0f, 180.0f))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye Max Angle changed: " << glm::degrees(fisheyeMaxAngleDeg_) << " deg";
            }

            if (ImGui::DragFloat("Center X", &fisheyeCx_, 0.5f, -500.0f, 500.0f, "%.1f px"))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye Center X changed: " << fisheyeCx_ << " px";
            }

            if (ImGui::DragFloat("Center Y", &fisheyeCy_, 0.5f, -500.0f, 500.0f, "%.1f px"))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye Center Y changed: " << fisheyeCy_ << " px";
            }

            if (ImGui::DragFloat("k1", &fisheyeK1_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye k1 changed: " << fisheyeK1_;
            }

            if (ImGui::DragFloat("k2", &fisheyeK2_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye k2 changed: " << fisheyeK2_;
            }

            if (ImGui::DragFloat("k3", &fisheyeK3_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye k3 changed: " << fisheyeK3_;
            }

            if (ImGui::DragFloat("k4", &fisheyeK4_, 0.001f, -1.0f, 1.0f, "%.4f"))
            {
                fisheyeParamsChanged_ = true;
                Log::INFO("GUI") << "Fisheye k4 changed: " << fisheyeK4_;
            }

            if (!isFisheye)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Switch to Fisheye camera to adjust");
            }

            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Depth of Field");
        ImGui::SameLine();
        if (ImGui::Checkbox("##DoFEnable", &dofEnabled_))
        {
            dofEnabledChanged_ = true;
            Log::INFO("GUI") << "DoF " << (dofEnabled_ ? "enabled" : "disabled");
        }

        ImGui::Spacing();

        if (ImGui::TreeNode("DoF Parameters"))
        {
            if (!dofEnabled_)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::SliderFloat("Aperture", &dofAperture_, 0.f, 0.5f))
            {
                dofParamsChanged_ = true;
                Log::INFO("GUI") << "DoF Aperture changed: " << dofAperture_;
            }
            if (ImGui::SliderFloat("Focal Distance", &dofFocalDistance_, 0.1f, 100.0f))
            {
                dofParamsChanged_ = true;
                Log::INFO("GUI") << "DoF Focal Distance changed: " << dofFocalDistance_;
            }

            if (!dofEnabled_)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Enable DoF to adjust");
            }

            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Shadow");
        ImGui::SameLine();
        if (ImGui::Checkbox("##ShadowEnable", &shadowEnabled_))
        {
            shadowEnabledChanged_ = true;
            Log::INFO("GUI") << "Shadow " << (shadowEnabled_ ? "enabled" : "disabled");
        }

        ImGui::Spacing();

        if (ImGui::TreeNode("Shadow Parameters"))
        {
            if (!shadowEnabled_)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::SliderFloat("Shadow Intensity", &shadowIntensity_, 0.0f, 1.0f, "%.2f"))
            {
                shadowParamsChanged_ = true;
                Log::INFO("GUI") << "Shadow Intensity changed: " << shadowIntensity_;
            }

            if (!shadowEnabled_)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Enable Shadow to adjust");
            }

            ImGui::TreePop();
        }
    }
    ImGui::End();
}


void ImGuiManager::showLoadingOverlay(float progress, const char* stageName, const char* fileName)
{
    ImGuiIO& io = ImGui::GetIO();

    // Semi-transparent fullscreen dim
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.1f, 0.75f));
    ImGui::Begin("##LoadingDim", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::End();
    ImGui::PopStyleColor();

    // Centered loading panel
    ImVec2 panelSize(440, 148);
    ImVec2 panelPos(
        (io.DisplaySize.x - panelSize.x) * 0.5f,
        (io.DisplaySize.y - panelSize.y) * 0.5f
    );

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panelSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 22));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.18f, 0.95f));

    ImGui::Begin("##LoadingPanel", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNav);

    // Title
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "Loading Scene");

    // File name
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%s", fileName);

    ImGui::Spacing();

    // Stage name
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "%s", stageName);

    ImGui::Spacing();

    // Progress bar with percentage
    char overlay[32];
    snprintf(overlay, sizeof(overlay), "%.0f%%", progress * 100.0f);

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.55f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.28f, 1.0f));
    ImGui::ProgressBar(progress, ImVec2(-1, 22), overlay);
    ImGui::PopStyleColor(2);

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
