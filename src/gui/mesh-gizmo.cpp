#include "mesh-gizmo.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>


static const glm::vec3 kWorldAxes[3] =
{
    glm::vec3(1.0f, 0.0f, 0.0f),  // X
    glm::vec3(0.0f, 1.0f, 0.0f),  // Y
    glm::vec3(0.0f, 0.0f, 1.0f),  // Z
};


glm::vec2 MeshGizmo::worldToScreen(const glm::vec3& worldPos,
                                    const glm::mat4& viewProj,
                                    const glm::vec2& displaySize) const
{
    glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
    if (clipPos.w <= 0.0001f)
    {
        return glm::vec2(-10000.0f);
    }

    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

    // Projection already has Vulkan Y-flip (proj[1][1] *= -1), so ndc.y is
    // already in screen-down convention.  Map [-1,+1] → [0, displaySize.y].
    return glm::vec2(
        (ndc.x + 1.0f) * 0.5f * displaySize.x,
        (ndc.y + 1.0f) * 0.5f * displaySize.y
    );
}


uint32_t MeshGizmo::getAxisColor(GizmoAxis axis) const
{
    if (axis == hoveredAxis_ || axis == activeAxis_)
    {
        return colorHighlight_;
    }

    switch (axis)
    {
        case GizmoAxis::X: return colorX_;
        case GizmoAxis::Y: return colorY_;
        case GizmoAxis::Z: return colorZ_;
        default:           return 0xFFFFFFFF;
    }
}


// ── Hit Testing ──────────────────────────────────────────────


GizmoAxis MeshGizmo::hitTestAxis(const glm::vec2& mousePos,
                                  const glm::vec2& screenOrigin,
                                  const glm::vec2 screenEnds[3]) const
{
    float minDist    = hoverThreshold_;
    GizmoAxis closest = GizmoAxis::NONE;

    for (int i = 0; i < 3; ++i)
    {
        glm::vec2 lineDir = screenEnds[i] - screenOrigin;
        float lineLen = glm::length(lineDir);
        if (lineLen < 1.0f)
        {
            continue;
        }

        glm::vec2 lineNorm = lineDir / lineLen;
        glm::vec2 toMouse  = mousePos - screenOrigin;

        float t = glm::clamp(glm::dot(toMouse, lineNorm) / lineLen, 0.0f, 1.0f);
        glm::vec2 closestPoint = screenOrigin + lineDir * t;

        float dist = glm::length(mousePos - closestPoint);
        if (dist < minDist)
        {
            minDist = dist;
            closest = static_cast<GizmoAxis>(i + 1);
        }
    }

    return closest;
}


GizmoAxis MeshGizmo::hitTestCircles(const glm::vec2& mousePos,
                                     const glm::vec2& screenOrigin,
                                     const glm::mat4& viewMatrix,
                                     const glm::mat4& viewProj,
                                     const glm::vec3& meshPosition,
                                     float worldRadius,
                                     const glm::vec2& displaySize) const
{
    float minDist    = hoverThreshold_ * 2.0f;
    GizmoAxis closest = GizmoAxis::NONE;

    // For each axis, sample points on the rotation circle and find min distance
    for (int axisIdx = 0; axisIdx < 3; ++axisIdx)
    {
        glm::vec3 normal = kWorldAxes[axisIdx];
        glm::vec3 tangent, bitangent;

        if (axisIdx == 0)       // X axis: circle in YZ plane
        {
            tangent   = glm::vec3(0.0f, 1.0f, 0.0f);
            bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else if (axisIdx == 1)  // Y axis: circle in XZ plane
        {
            tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
            bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else                    // Z axis: circle in XY plane
        {
            tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
            bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        float bestSegDist = 100000.0f;

        constexpr int kSamples = 32;
        for (int s = 0; s < kSamples; ++s)
        {
            float angle = static_cast<float>(s) / static_cast<float>(kSamples) * glm::two_pi<float>();
            glm::vec3 worldPt = meshPosition
                              + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * worldRadius;
            glm::vec2 screenPt = worldToScreen(worldPt, viewProj, displaySize);

            float dist = glm::length(mousePos - screenPt);
            if (dist < bestSegDist)
            {
                bestSegDist = dist;
            }
        }

        if (bestSegDist < minDist)
        {
            minDist = bestSegDist;
            closest = static_cast<GizmoAxis>(axisIdx + 1);
        }
    }

    return closest;
}


// ── Drawing ──────────────────────────────────────────────────


void MeshGizmo::drawTranslateGizmo(ImDrawList* drawList,
                                    const glm::vec2& screenOrigin,
                                    const glm::vec2 screenEnds[3]) const
{
    ImVec2 origin(screenOrigin.x, screenOrigin.y);

    // Sort by depth (z-component in view space) for correct overlap
    // For simplicity, draw all 3 axes with priority to the hovered/active one
    for (int i = 0; i < 3; ++i)
    {
        GizmoAxis axis = static_cast<GizmoAxis>(i + 1);
        uint32_t color = getAxisColor(axis);
        float width = (axis == activeAxis_ || axis == hoveredAxis_) ? lineWidth_ + 1.0f : lineWidth_;

        glm::vec2 dir = screenEnds[i] - screenOrigin;
        float len = glm::length(dir);
        if (len < 2.0f)
        {
            continue;
        }

        ImVec2 endPt(screenEnds[i].x, screenEnds[i].y);

        // Axis line
        drawList->AddLine(origin, endPt, color, width);

        // Arrowhead
        glm::vec2 dirN = dir / len;
        glm::vec2 perp(-dirN.y, dirN.x);

        ImVec2 tip(endPt.x + dirN.x * arrowLength_,
                   endPt.y + dirN.y * arrowLength_);
        ImVec2 left(endPt.x + perp.x * arrowWidth_,
                    endPt.y + perp.y * arrowWidth_);
        ImVec2 right(endPt.x - perp.x * arrowWidth_,
                     endPt.y - perp.y * arrowWidth_);
        drawList->AddTriangleFilled(tip, left, right, color);

        // Axis label
        const char* labels[] = {"X", "Y", "Z"};
        ImVec2 labelPos(tip.x + dirN.x * 10.0f,
                        tip.y + dirN.y * 10.0f);
        ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
        labelPos.x -= textSize.x * 0.5f;
        labelPos.y -= textSize.y * 0.5f;
        drawList->AddText(labelPos, color, labels[i]);
    }

    // Center dot
    drawList->AddCircleFilled(origin, 4.0f, 0xFFCCCCCC);
}


void MeshGizmo::drawRotateGizmo(ImDrawList* drawList,
                                 const glm::vec2& screenOrigin,
                                 const glm::mat4& viewProj,
                                 const glm::vec3& meshPosition,
                                 float worldRadius,
                                 const glm::vec2& displaySize) const
{
    struct CircleData
    {
        GizmoAxis axis;
        glm::vec3 tangent;
        glm::vec3 bitangent;
    };

    CircleData circles[3] =
    {
        { GizmoAxis::X, {0,1,0}, {0,0,1} },
        { GizmoAxis::Y, {1,0,0}, {0,0,1} },
        { GizmoAxis::Z, {1,0,0}, {0,1,0} },
    };

    for (const auto& circle : circles)
    {
        uint32_t color = getAxisColor(circle.axis);
        float width = (circle.axis == activeAxis_ || circle.axis == hoveredAxis_)
                    ? circleWidthPx_ + 1.5f
                    : circleWidthPx_;

        std::vector<ImVec2> points;
        points.reserve(circleSegments_ + 1);

        for (int s = 0; s <= circleSegments_; ++s)
        {
            float angle = static_cast<float>(s) / static_cast<float>(circleSegments_)
                        * glm::two_pi<float>();
            glm::vec3 worldPt = meshPosition
                              + (circle.tangent * std::cos(angle)
                              +  circle.bitangent * std::sin(angle)) * worldRadius;
            glm::vec2 sp = worldToScreen(worldPt, viewProj, displaySize);
            points.push_back(ImVec2(sp.x, sp.y));
        }

        if (points.size() >= 2)
        {
            drawList->AddPolyline(points.data(),
                                  static_cast<int>(points.size()),
                                  color, ImDrawFlags_None, width);
        }
    }

    // Center dot
    ImVec2 origin(screenOrigin.x, screenOrigin.y);
    drawList->AddCircleFilled(origin, 4.0f, 0xFFCCCCCC);
}


void MeshGizmo::drawScaleGizmo(ImDrawList* drawList,
                                const glm::vec2& screenOrigin,
                                const glm::vec2 screenEnds[3]) const
{
    ImVec2 origin(screenOrigin.x, screenOrigin.y);

    for (int i = 0; i < 3; ++i)
    {
        GizmoAxis axis = static_cast<GizmoAxis>(i + 1);
        uint32_t color = getAxisColor(axis);
        float width = (axis == activeAxis_ || axis == hoveredAxis_) ? lineWidth_ + 1.0f : lineWidth_;

        glm::vec2 dir = screenEnds[i] - screenOrigin;
        float len = glm::length(dir);
        if (len < 2.0f)
        {
            continue;
        }

        ImVec2 endPt(screenEnds[i].x, screenEnds[i].y);

        // Axis line
        drawList->AddLine(origin, endPt, color, width);

        // Scale box at the end
        ImVec2 boxMin(endPt.x - scaleBoxSize_, endPt.y - scaleBoxSize_);
        ImVec2 boxMax(endPt.x + scaleBoxSize_, endPt.y + scaleBoxSize_);
        drawList->AddRectFilled(boxMin, boxMax, color);

        // Axis label
        const char* labels[] = {"X", "Y", "Z"};
        glm::vec2 dirN = dir / len;
        ImVec2 labelPos(endPt.x + dirN.x * 14.0f,
                        endPt.y + dirN.y * 14.0f);
        ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
        labelPos.x -= textSize.x * 0.5f;
        labelPos.y -= textSize.y * 0.5f;
        drawList->AddText(labelPos, color, labels[i]);
    }

    // Center dot
    drawList->AddCircleFilled(origin, 4.0f, 0xFFCCCCCC);
}


// ── Main draw / handleInput ──────────────────────────────────


void MeshGizmo::draw(const glm::mat4& viewMatrix,
                      const glm::mat4& projMatrix,
                      const glm::vec3& meshPosition,
                      const glm::vec2& displaySize)
{
    if (!visible_)
    {
        return;
    }

    glm::mat4 viewProj = projMatrix * viewMatrix;

    // Check if mesh is behind camera
    glm::vec4 clipPos = viewProj * glm::vec4(meshPosition, 1.0f);
    if (clipPos.w <= 0.001f)
    {
        return;
    }

    glm::vec2 screenOrigin = worldToScreen(meshPosition, viewProj, displaySize);

    // Compute world-space axis length proportional to camera distance
    glm::vec3 cameraPos = glm::vec3(glm::inverse(viewMatrix)[3]);
    float cameraDist    = glm::length(cameraPos - meshPosition);
    float worldLen      = cameraDist * axisWorldScale_;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    if (mode_ == GizmoMode::TRANSLATE || mode_ == GizmoMode::SCALE)
    {
        glm::vec2 screenEnds[3];
        for (int i = 0; i < 3; ++i)
        {
            glm::vec3 worldEnd = meshPosition + kWorldAxes[i] * worldLen;
            screenEnds[i] = worldToScreen(worldEnd, viewProj, displaySize);
        }

        if (mode_ == GizmoMode::TRANSLATE)
        {
            drawTranslateGizmo(drawList, screenOrigin, screenEnds);
        }
        else
        {
            drawScaleGizmo(drawList, screenOrigin, screenEnds);
        }
    }
    else if (mode_ == GizmoMode::ROTATE)
    {
        drawRotateGizmo(drawList, screenOrigin, viewProj,
                        meshPosition, worldLen, displaySize);
    }
}


bool MeshGizmo::handleInput(const glm::mat4& viewMatrix,
                             const glm::mat4& projMatrix,
                             const glm::vec3& cameraPos,
                             const glm::vec3& meshPosition,
                             const glm::vec2& displaySize,
                             glm::vec3& outPosition,
                             glm::vec3& outRotation,
                             glm::vec3& outScale)
{
    if (!visible_)
    {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);

    glm::mat4 viewProj = projMatrix * viewMatrix;

    // Check if mesh is behind camera
    glm::vec4 clipPos = viewProj * glm::vec4(meshPosition, 1.0f);
    if (clipPos.w <= 0.001f)
    {
        hoveredAxis_ = GizmoAxis::NONE;
        return false;
    }

    glm::vec2 screenOrigin = worldToScreen(meshPosition, viewProj, displaySize);
    float cameraDist = glm::length(cameraPos - meshPosition);
    float worldLen   = cameraDist * axisWorldScale_;

    // ── Hover detection ──

    if (!isDragging_)
    {
        if (mode_ == GizmoMode::TRANSLATE || mode_ == GizmoMode::SCALE)
        {
            glm::vec2 screenEnds[3];
            for (int i = 0; i < 3; ++i)
            {
                glm::vec3 worldEnd = meshPosition + kWorldAxes[i] * worldLen;
                screenEnds[i] = worldToScreen(worldEnd, viewProj, displaySize);
            }
            hoveredAxis_ = hitTestAxis(mousePos, screenOrigin, screenEnds);
        }
        else if (mode_ == GizmoMode::ROTATE)
        {
            hoveredAxis_ = hitTestCircles(mousePos, screenOrigin, viewMatrix,
                                           viewProj, meshPosition, worldLen, displaySize);
        }
    }

    // ── Drag start ──

    if (!isDragging_ && hoveredAxis_ != GizmoAxis::NONE && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        isDragging_        = true;
        activeAxis_        = hoveredAxis_;
        dragStartMouse_    = mousePos;
        dragStartPosition_ = outPosition;
        dragStartRotation_ = outRotation;
        dragStartScale_    = outScale;

        if (mode_ == GizmoMode::ROTATE)
        {
            dragStartAngle_ = std::atan2(mousePos.y - screenOrigin.y,
                                          mousePos.x - screenOrigin.x);
        }
    }

    // ── Drag processing ──

    if (isDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        glm::vec2 mouseDelta = mousePos - dragStartMouse_;
        int axisIdx = static_cast<int>(activeAxis_) - 1;  // 0=X, 1=Y, 2=Z

        if (mode_ == GizmoMode::TRANSLATE)
        {
            // Project mouse delta onto screen-space axis direction
            glm::vec2 screenEnd = worldToScreen(
                meshPosition + kWorldAxes[axisIdx] * worldLen, viewProj, displaySize);
            glm::vec2 screenDir = screenEnd - screenOrigin;
            float screenLen = glm::length(screenDir);

            if (screenLen > 1.0f)
            {
                glm::vec2 screenDirN = screenDir / screenLen;
                float projected = glm::dot(mouseDelta, screenDirN);
                float worldDelta = projected * translateSensitivity_ * cameraDist;

                outPosition = dragStartPosition_ + kWorldAxes[axisIdx] * worldDelta;
            }

            return true;
        }
        else if (mode_ == GizmoMode::ROTATE)
        {
            float currentAngle = std::atan2(mousePos.y - screenOrigin.y,
                                             mousePos.x - screenOrigin.x);
            float deltaAngle = currentAngle - dragStartAngle_;

            // Normalize to [-π, π] to avoid atan2 discontinuity at ±π
            while (deltaAngle >  glm::pi<float>()) deltaAngle -= glm::two_pi<float>();
            while (deltaAngle < -glm::pi<float>()) deltaAngle += glm::two_pi<float>();

            outRotation = dragStartRotation_;
            outRotation[axisIdx] -= glm::degrees(deltaAngle) * rotateSensitivity_;

            return true;
        }
        else if (mode_ == GizmoMode::SCALE)
        {
            glm::vec2 screenEnd = worldToScreen(
                meshPosition + kWorldAxes[axisIdx] * worldLen, viewProj, displaySize);
            glm::vec2 screenDir = screenEnd - screenOrigin;
            float screenLen = glm::length(screenDir);

            if (screenLen > 1.0f)
            {
                glm::vec2 screenDirN = screenDir / screenLen;
                float projected = glm::dot(mouseDelta, screenDirN);

                outScale = dragStartScale_;
                outScale[axisIdx] += projected * scaleSensitivity_;
                outScale[axisIdx] = glm::max(outScale[axisIdx], 0.01f);
            }

            return true;
        }
    }

    // ── Drag end ──

    if (isDragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        isDragging_ = false;
        activeAxis_ = GizmoAxis::NONE;
    }

    return false;
}
