#include "axis-gizmo.h"

#include <imgui.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>


void AxisGizmo::draw(const glm::mat4& viewMatrix)
{
    glm::mat3 rotation = glm::mat3(viewMatrix);

    struct AxisDrawData
    {
        int         axisIndex;
        glm::vec2   screenDir;
        float       depth;
        float       projLength;
        uint32_t    color;
        const char* label;
    };

    const glm::vec3 worldAxes[3] =
    {
        glm::vec3(1.0f, 0.0f, 0.0f),  // X
        glm::vec3(0.0f, 1.0f, 0.0f),  // Y
        glm::vec3(0.0f, 0.0f, 1.0f),  // Z
    };

    const uint32_t    colors[3] = { colorX_, colorY_, colorZ_ };
    const char* const labels[3] = { "X", "Y", "Z" };

    std::array<AxisDrawData, 3> axes;
    for (int i = 0; i < 3; ++i)
    {
        glm::vec3 camAxis = rotation * worldAxes[i];
        axes[i].axisIndex  = i;
        axes[i].screenDir  = glm::vec2(camAxis.x, -camAxis.y);
        axes[i].depth      = camAxis.z;
        axes[i].projLength = glm::length(axes[i].screenDir);
        axes[i].color      = colors[i];
        axes[i].label      = labels[i];
    }

    ImGuiIO& io = ImGui::GetIO();
    float centerX = padding_;
    float centerY = io.DisplaySize.y - padding_;
    ImVec2 center(centerX, centerY);

    std::sort(axes.begin(), axes.end(), [](const AxisDrawData& a, const AxisDrawData& b)
    {
        return a.depth > b.depth;
    });

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    for (const auto& axis : axes)
    {
        // Compute alpha based on depth [-1, +1] -> [front, back]
        float normalizedDepth = glm::clamp((axis.depth + 1.0f) * 0.5f, 0.0f, 1.0f);
        float alpha = alphaFront_ + normalizedDepth * (alphaBack_ - alphaFront_);
        alpha = glm::clamp(alpha, alphaBack_, alphaFront_);

        // Apply alpha to color (ABGR format: 0xAABBGGRR)
        uint32_t a        = static_cast<uint32_t>(alpha * 255.0f) & 0xFF;
        uint32_t modColor = (axis.color & 0x00FFFFFF) | (a << 24);

        // Edge case: axis pointing directly at/away from camera
        float projThreshold = minProjLength_ / axisLength_;
        if (axis.projLength < projThreshold)
        {
            // Draw a small dot at center to indicate axis exists
            drawList->AddCircleFilled(center, originRadius_ * 0.6f, modColor);
            continue;
        }

        // Normalized screen direction and perpendicular
        glm::vec2 dir  = axis.screenDir / axis.projLength;
        glm::vec2 perp = glm::vec2(-dir.y, dir.x);

        // Scale by projection length for foreshortening effect
        float scaledLength = axisLength_ * axis.projLength;

        // Axis line: center → endPoint
        ImVec2 endPoint(
            center.x + dir.x * scaledLength,
            center.y + dir.y * scaledLength
        );
        drawList->AddLine(center, endPoint, modColor, lineWidth_);

        // Arrowhead triangle: tip, leftBase, rightBase
        ImVec2 tip(
            endPoint.x + dir.x * arrowLength_,
            endPoint.y + dir.y * arrowLength_
        );
        ImVec2 leftBase(
            endPoint.x + perp.x * arrowWidth_,
            endPoint.y + perp.y * arrowWidth_
        );
        ImVec2 rightBase(
            endPoint.x - perp.x * arrowWidth_,
            endPoint.y - perp.y * arrowWidth_
        );
        drawList->AddTriangleFilled(tip, leftBase, rightBase, modColor);

        // Label text (centered on position)
        ImVec2 labelPos(
            tip.x + dir.x * labelOffset_,
            tip.y + dir.y * labelOffset_
        );
        ImVec2 textSize = ImGui::CalcTextSize(axis.label);
        labelPos.x -= textSize.x * 0.5f;
        labelPos.y -= textSize.y * 0.5f;
        drawList->AddText(labelPos, modColor, axis.label);
    }

    drawList->AddCircleFilled(center, originRadius_, colorOrigin_);
}