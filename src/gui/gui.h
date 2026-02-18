#ifndef GUI_H
#define GUI_H

#include "axis-gizmo.h"

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

    // Mesh
    bool insertTeapot_          = false;
    int  removeMeshIndex_       = -1;
    int  meshMaterialChangeIdx_ = -1;
    int  meshMaterialNewType_   = 0;

    // Fisheye parameters (SliderAngle stores radians internally, displays degrees)
    float fisheyeFovDeg_        = 3.14159265f;  // PI radians = 180°
    float fisheyeMaxAngleDeg_   = 1.57079632f;  // PI/2 radians = 90°
    float fisheyeCx_            = 0.0f;         // pixel offset
    float fisheyeCy_            = 0.0f;         // pixel offset
    float fisheyeK1_            = 0.0f;         // distortion coefficient
    float fisheyeK2_            = 0.0f;
    float fisheyeK3_            = 0.0f;
    float fisheyeK4_            = 0.0f;
    bool  fisheyeParamsChanged_ = false;

    // Axis gizmo
    AxisGizmo axisGizmo_;

    // Depth of Field parameters
    bool  dofEnabled_        = false;
    bool  dofEnabledChanged_ = false;
    float dofAperture_       = 0.05f;
    float dofFocalDistance_  = 5.0f; 
    bool  dofParamsChanged_  = false;

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
    void showAxisGizmo(const glm::mat4& viewMatrix);

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

    // Mesh
    bool shouldInsertTeapot() const { return insertTeapot_; }
    void clearInsertTeapot() { insertTeapot_ = false; }
    int  getRemoveMeshIndex() const { return removeMeshIndex_; }
    void clearRemoveMeshIndex() { removeMeshIndex_ = -1; }
    int  getMeshMaterialChangeIndex() const { return meshMaterialChangeIdx_; }
    int  getMeshMaterialNewType() const { return meshMaterialNewType_; }
    void clearMeshMaterialChange() { meshMaterialChangeIdx_ = -1; }

    // Fisheye parameters
    float getFisheyeFovDeg() const { return fisheyeFovDeg_; }
    float getFisheyeMaxAngleDeg() const { return fisheyeMaxAngleDeg_; }
    float getFisheyeCx() const { return fisheyeCx_; }
    float getFisheyeCy() const { return fisheyeCy_; }
    float getFisheyeK1() const { return fisheyeK1_; }
    float getFisheyeK2() const { return fisheyeK2_; }
    float getFisheyeK3() const { return fisheyeK3_; }
    float getFisheyeK4() const { return fisheyeK4_; }
    bool  isFisheyeParamsChanged() const { return fisheyeParamsChanged_; }
    void  clearFisheyeParamsChanged() { fisheyeParamsChanged_ = false; }

    // Depth of Field parameters
    bool  isDoFEnabled() const { return dofEnabled_; }
    bool  isDoFEnabledChanged() const { return dofEnabledChanged_; }
    float getDoFAperture() const { return dofAperture_; }
    float getDoFFocalDistance() const { return dofFocalDistance_; }
    bool  isDoFParamsChanged() const { return dofParamsChanged_; }
    void  clearDoFChanged() { dofEnabledChanged_ = false; dofParamsChanged_ = false; }
};

#endif // GUI_H