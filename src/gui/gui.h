#ifndef GUI_H
#define GUI_H

#include <vulkan/vulkan.h>

#include <cstdint>


struct GLFWwindow;
struct VkContext;
struct VkSwapchain;


constexpr uint32_t GUI_RENDER_MODE_GS    = 0;  // Gaussian Splatting
constexpr uint32_t GUI_RENDER_MODE_POINT = 1;  // Point visualization
constexpr uint32_t GUI_RENDER_MODE_SPLAT = 2;  // Splat visualization


class ImGuiManager
{
    VkContext*   context_   = nullptr;
    VkSwapchain* swapchain_ = nullptr;

    bool initialized_    = false;
    bool showFpsOverlay_ = true;
    bool showRightPanel_ = true;

    float rightPanelWidth_ = 300.0f;

    // Render mode
    uint32_t renderMode_        = GUI_RENDER_MODE_GS;
    bool     renderModeChanged_ = false;

    // SH degree
    int  shDegree_        = 0;
    bool shDegreeChanged_ = false;
    bool shAvailable_     = false;

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

    // Settings
    void setShowFpsOverlay(bool show) { showFpsOverlay_ = show; }
    bool isShowFpsOverlay() const { return showFpsOverlay_; }

    void setShowRightPanel(bool show) { showRightPanel_ = show; }
    bool isShowRightPanel() const { return showRightPanel_; }

    void setRightPanelWidth(float width) { rightPanelWidth_ = width; }
    float getRightPanelWidth() const { return rightPanelWidth_; }

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
};

#endif // GUI_H