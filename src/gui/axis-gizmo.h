#ifndef AXIS_GIZMO_H
#define AXIS_GIZMO_H

#include <glm/glm.hpp>

#include <cstdint>


class AxisGizmo
{
    // Rendering parameters
    float    axisLength_    = 50.0f;    // axis line length (px)
    float    padding_       = 70.0f;    // padding from screen edges (px)
    float    lineWidth_     = 2.5f;     // axis line thickness (px)
    float    arrowLength_   = 10.0f;    // arrowhead length (px)
    float    arrowWidth_    = 6.0f;     // arrowhead half-width (px)
    float    originRadius_  = 3.0f;     // center dot radius (px)
    float    labelOffset_   = 14.0f;    // label distance from arrow tip (px)

    // Axis colors (ImGui ABGR format: 0xAABBGGRR)
    uint32_t colorX_        = 0xFF4444FF;  // Red    (X axis)
    uint32_t colorY_        = 0xFF44DD44;  // Green  (Y axis)
    uint32_t colorZ_        = 0xFFFF8844;  // Blue   (Z axis)
    uint32_t colorOrigin_   = 0xFFCCCCCC;  // Light gray (center dot)

    // Alpha modulation range for depth effect
    float    alphaFront_    = 1.0f;     // alpha for axis facing toward camera
    float    alphaBack_     = 0.25f;    // alpha for axis facing away from camera

    // Minimum projection length threshold (edge case)
    float    minProjLength_ = 3.0f;     // pixels

    // Visibility
    bool     visible_       = true;

public:
    AxisGizmo()  = default;
    ~AxisGizmo() = default;

    AxisGizmo(const AxisGizmo&) = default;
    AxisGizmo& operator=(const AxisGizmo&) = default;

    void draw(const glm::mat4& viewProjMatrix);

    float getAxisLength() const { return axisLength_; }
    void setAxisLength(float length) { axisLength_ = length; }
    float getPadding() const { return padding_; }
    void setPadding(float padding) { padding_ = padding; }
};

#endif // AXIS_GIZMO_H