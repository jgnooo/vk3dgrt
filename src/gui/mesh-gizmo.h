#ifndef MESH_GIZMO_H
#define MESH_GIZMO_H

#include <glm/glm.hpp>

#include <cstdint>


struct ImDrawList;


enum class GizmoMode
{
    TRANSLATE = 0,
    ROTATE    = 1,
    SCALE     = 2,
};


enum class GizmoAxis
{
    NONE = 0,
    X    = 1,
    Y    = 2,
    Z    = 3,
};


class MeshGizmo
{
    // Gizmo state
    GizmoMode mode_        = GizmoMode::TRANSLATE;
    GizmoAxis hoveredAxis_ = GizmoAxis::NONE;
    GizmoAxis activeAxis_  = GizmoAxis::NONE;
    bool      isDragging_  = false;

    // Drag state
    glm::vec2 dragStartMouse_    = glm::vec2(0.0f);
    glm::vec3 dragStartPosition_ = glm::vec3(0.0f);
    glm::vec3 dragStartRotation_ = glm::vec3(0.0f);
    glm::vec3 dragStartScale_    = glm::vec3(1.0f);
    float     dragStartAngle_    = 0.0f;

    // Rendering parameters
    float axisWorldScale_  = 0.15f;     // gizmo size as fraction of camera distance
    float lineWidth_       = 3.0f;      // axis line thickness (px)
    float arrowLength_     = 12.0f;     // arrowhead length (px)
    float arrowWidth_      = 7.0f;      // arrowhead half-width (px)
    float hoverThreshold_  = 10.0f;     // mouse distance threshold for hover (px)
    int   circleSegments_  = 64;        // rotation circle segments
    float circleWidthPx_   = 2.5f;     // rotation circle line width (px)
    float scaleBoxSize_    = 5.0f;      // scale box half-size (px)

    // Colors (ImGui ABGR format: 0xAABBGGRR)
    uint32_t colorX_         = 0xFF4444FF;  // Red    (X axis)
    uint32_t colorY_         = 0xFF44DD44;  // Green  (Y axis)
    uint32_t colorZ_         = 0xFFFF8844;  // Blue   (Z axis)
    uint32_t colorHighlight_ = 0xFF00FFFF;  // Yellow (hovered/active)

    // Sensitivity
    float translateSensitivity_ = 0.002f;
    float rotateSensitivity_    = 0.15f;    // degrees per pixel
    float scaleSensitivity_     = 0.002f;

    // Visibility
    bool visible_ = true;

public:
    MeshGizmo()  = default;
    ~MeshGizmo() = default;

    MeshGizmo(const MeshGizmo&)            = default;
    MeshGizmo& operator=(const MeshGizmo&) = default;

    // Draw the gizmo overlay at the mesh position.
    // Must be called between ImGui::NewFrame() and ImGui::Render().
    void draw(const glm::mat4& viewMatrix,
              const glm::mat4& projMatrix,
              const glm::vec3& meshPosition,
              const glm::vec2& displaySize);

    // Process mouse input and compute new transform values.
    // Returns true if the transform was modified.
    bool handleInput(const glm::mat4& viewMatrix,
                     const glm::mat4& projMatrix,
                     const glm::vec3& cameraPos,
                     const glm::vec3& meshPosition,
                     const glm::vec2& displaySize,
                     glm::vec3& outPosition,
                     glm::vec3& outRotation,
                     glm::vec3& outScale);

    // Mode control
    void      setMode(GizmoMode mode) { mode_ = mode; }
    GizmoMode getMode() const { return mode_; }

    // State queries
    bool isDragging() const { return isDragging_; }
    bool isHovered() const { return hoveredAxis_ != GizmoAxis::NONE; }

    // Visibility
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

private:
    // Coordinate conversion
    glm::vec2 worldToScreen(const glm::vec3& worldPos,
                            const glm::mat4& viewProj,
                            const glm::vec2& displaySize) const;

    // Hit testing
    GizmoAxis hitTestAxis(const glm::vec2& mousePos,
                          const glm::vec2& screenOrigin,
                          const glm::vec2 screenEnds[3]) const;

    GizmoAxis hitTestCircles(const glm::vec2& mousePos,
                             const glm::vec2& screenOrigin,
                             const glm::mat4& viewMatrix,
                             const glm::mat4& viewProj,
                             const glm::vec3& meshPosition,
                             float worldRadius,
                             const glm::vec2& displaySize) const;

    // Drawing helpers
    void drawTranslateGizmo(ImDrawList* drawList,
                            const glm::vec2& screenOrigin,
                            const glm::vec2 screenEnds[3]) const;

    void drawRotateGizmo(ImDrawList* drawList,
                         const glm::vec2& screenOrigin,
                         const glm::mat4& viewProj,
                         const glm::vec3& meshPosition,
                         float worldRadius,
                         const glm::vec2& displaySize) const;

    void drawScaleGizmo(ImDrawList* drawList,
                        const glm::vec2& screenOrigin,
                        const glm::vec2 screenEnds[3]) const;

    // Color helpers
    uint32_t getAxisColor(GizmoAxis axis) const;
};

#endif // MESH_GIZMO_H
