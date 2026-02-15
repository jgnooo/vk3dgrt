#ifndef GUI_H
#define GUI_H

#include <vulkan/vulkan.h>

#include <cstdint>


struct GLFWwindow;
struct VkContext;
struct VkSwapchain;

namespace vk3dgrt { class GRTScene; }


constexpr uint32_t GUI_RENDER_MODE_GS    = 0;  // Gaussian Splatting
constexpr uint32_t GUI_RENDER_MODE_POINT = 1;  // Point visualization
constexpr uint32_t GUI_RENDER_MODE_SPLAT = 2;  // Splat visualization

constexpr uint32_t GUI_CAMERA_PINHOLE = 0;
constexpr uint32_t GUI_CAMERA_FISHEYE = 1;


class ImGuiManager
{
    VkContext*          context_   = nullptr;
    VkSwapchain*        swapchain_ = nullptr;
    vk3dgrt::GRTScene*  scene_     = nullptr;

    bool initialized_    = false;
    bool showFpsOverlay_ = true;
    bool showRightPanel_ = true;

    float rightPanelWidth_ = 300.0f;

    // Camera type
    uint32_t cameraType_        = GUI_CAMERA_PINHOLE;
    bool     cameraTypeChanged_ = false;

    // Render mode
    uint32_t renderMode_        = GUI_RENDER_MODE_GS;
    bool     renderModeChanged_ = false;

    // SH degree
    int  shDegree_        = 0;
    bool shDegreeChanged_ = false;
    bool shAvailable_     = false;

    // Reflection settings
    bool reflectEnabled_        = false;
    bool reflectEnabledChanged_ = false;
    int  maxBounces_            = 1;
    bool maxBouncesChanged_     = false;

    // Mesh
    bool insertTeapot_    = false;
    int  removeMeshIndex_ = -1;

public:
    void initialize(GLFWwindow* window,
                    VkContext* context,
                    VkSwapchain* swapchain);
    void shutdown();

    // Frame handling
    void newFrame();
    void render();
    void renderDrawData(VkCommandBuffer cmdBuffer);

    // UI panels
    void showFpsOverlay();
    void showRightPanel();

    // Scene access (for Assets panel)
    void setScene(vk3dgrt::GRTScene* scene) { scene_ = scene; }

    // Settings
    void setShowFpsOverlay(bool show) { showFpsOverlay_ = show; }
    bool isShowFpsOverlay() const { return showFpsOverlay_; }

    void setShowRightPanel(bool show) { showRightPanel_ = show; }
    bool isShowRightPanel() const { return showRightPanel_; }

    void setRightPanelWidth(float width) { rightPanelWidth_ = width; }
    float getRightPanelWidth() const { return rightPanelWidth_; }

    // Camera type
    void setCameraType(uint32_t type) { cameraType_ = type; }
    uint32_t getCameraType() const { return cameraType_; }
    bool isCameraTypeChanged() const { return cameraTypeChanged_; }
    void clearCameraTypeChanged() { cameraTypeChanged_ = false; }

    // Render mode
    void setRenderMode(uint32_t mode) { renderMode_ = mode; }
    uint32_t getRenderMode() const { return renderMode_; }
    bool isRenderModeChanged() const { return renderModeChanged_; }
    void clearRenderModeChanged() { renderModeChanged_ = false; }

    // SH degree
    void setSHDegree(int degree) { shDegree_ = degree; }
    int  getSHDegree() const { return shDegree_; }
    bool isSHDegreeChanged() const { return shDegreeChanged_; }
    void clearSHDegreeChanged() { shDegreeChanged_ = false; }
    void setSHAvailable(bool available) { shAvailable_ = available; }

    // Reflection settings
    void setReflectionEnabled(bool enabled) { reflectEnabled_ = enabled; }
    bool isReflectionEnabled() const { return reflectEnabled_; }
    bool isReflectionEnabledChanged() const { return reflectEnabledChanged_; }
    void clearReflectionEnabledChanged() { reflectEnabledChanged_ = false; }

    void setMaxBounces(int bounces) { maxBounces_ = bounces; }
    int  getMaxBounces() const { return maxBounces_; }
    bool isMaxBouncesChanged() const { return maxBouncesChanged_; }
    void clearMaxBouncesChanged() { maxBouncesChanged_ = false; }

    // Mesh
    bool shouldInsertTeapot() const { return insertTeapot_; }
    void clearInsertTeapot() { insertTeapot_ = false; }
    int  getRemoveMeshIndex() const { return removeMeshIndex_; }
    void clearRemoveMeshIndex() { removeMeshIndex_ = -1; }
};

#endif // GUI_H