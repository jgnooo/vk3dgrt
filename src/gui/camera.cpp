#include "camera.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include <algorithm>
#include <cmath>


namespace vk3dgrt {

CameraController* CameraController::sInstance_ = nullptr;


// --------------------------------------------------- //
//  Camera Implementation
// --------------------------------------------------- //

glm::mat4 Camera::getViewMatrix() const
{
    return glm::lookAt(position, target, up);
}


glm::mat4 Camera::getProjectionMatrix() const
{
    // perspectiveRH_ZO: Right-Handed, Z range [0,1] (Vulkan depth convention)
    // Y-flip required: Vulkan clip space has Y+ pointing down, but our scene data
    // is in RUB convention (Y-up) after RDF->RUB conversion (like nvpro).
    // The Y-flip in inverse projection maps top-of-screen to +Y (up) correctly.
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(fovY), aspect, zNear, zFar);
    proj[1][1] *= -1.0f;
    return proj;
}


glm::mat4 Camera::getViewProjectionMatrix() const
{
    return getProjectionMatrix() * getViewMatrix();
}


glm::mat4 Camera::getInverseViewMatrix() const
{
    return glm::inverse(getViewMatrix());
}


glm::mat4 Camera::getInverseProjectionMatrix() const
{
    return glm::inverse(getProjectionMatrix());
}


glm::mat4 Camera::getInverseViewProjectionMatrix() const
{
    return glm::inverse(getViewProjectionMatrix());
}


glm::vec3 Camera::getForward() const
{
    return glm::normalize(target - position);
}


glm::vec3 Camera::getRight() const
{
    return glm::normalize(glm::cross(getForward(), up));
}


glm::vec3 Camera::getUp() const
{
    // Return the actual up vector (may differ from world up)
    return glm::normalize(glm::cross(getRight(), getForward()));
}


void Camera::setAspect(float newAspect)
{
    aspect = newAspect;
}


void Camera::setAspect(uint32_t width, uint32_t height)
{
    if (height > 0)
    {
        aspect = static_cast<float>(width) / static_cast<float>(height);
    }
}


void Camera::lookAt(const glm::vec3& newTarget)
{
    target = newTarget;
}


void Camera::lookAt(const glm::vec3& newPosition, const glm::vec3& newTarget, const glm::vec3& newUp)
{
    position = newPosition;
    target   = newTarget;
    up       = newUp;
}


void Camera::setPosition(const glm::vec3& newPosition)
{
    position = newPosition;
}


float Camera::getDistanceToTarget() const
{
    return glm::length(target - position);
}


// --------------------------------------------------- //
//  CameraController Implementation
// --------------------------------------------------- //

void CameraController::initialize(GLFWwindow* window, Camera* camera)
{
    window_ = window;
    camera_ = camera;

    // Store instance for static GLFW callbacks
    // (DO NOT use glfwSetWindowUserPointer — VkEngine owns it)
    sInstance_ = this;

    // Set up callbacks
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetScrollCallback(window_, scrollCallback);
    glfwSetKeyCallback(window_, keyCallback);

    // Initialize orbit parameters from camera
    updateOrbitAnglesFromCamera();

    // Get initial cursor position
    double xpos, ypos;
    glfwGetCursorPos(window_, &xpos, &ypos);
    lastMouseX_ = static_cast<float>(xpos);
    lastMouseY_ = static_cast<float>(ypos);
}


void CameraController::shutdown()
{
    if (window_)
    {
        // Clear callbacks (optional, window may be destroyed anyway)
        glfwSetMouseButtonCallback(window_, nullptr);
        glfwSetCursorPosCallback(window_, nullptr);
        glfwSetScrollCallback(window_, nullptr);
        glfwSetKeyCallback(window_, nullptr);
    }

    sInstance_ = nullptr;
    window_    = nullptr;
    camera_    = nullptr;
}


void CameraController::update(float deltaTime)
{
    if (!window_ || !camera_)
    {
        return;
    }

    // Skip keyboard movement only when ImGui text input is active
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }

    handleKeyboardMovement(deltaTime);
}


void CameraController::resetToDefault()
{
    if (!camera_)
    {
        return;
    }

    camera_->position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera_->target   = glm::vec3(0.0f, 0.0f, 0.0f);
    camera_->up       = glm::vec3(0.0f, 1.0f, 0.0f);

    orbitYaw_      = 0.0f;
    orbitPitch_    = 0.0f;
    orbitDistance_ = 5.0f;
}


void CameraController::focusOnBounds(const glm::vec3& minBound, const glm::vec3& maxBound)
{
    if (!camera_)
    {
        return;
    }

    // Calculate center and bounding sphere radius
    glm::vec3 center   = (minBound + maxBound) * 0.5f;
    glm::vec3 halfSize = (maxBound - minBound) * 0.5f;
    float radius       = glm::length(halfSize);

    // FOV-based distance calculation (consider both X and Y FOV)
    float yfov          = std::tan(glm::radians(camera_->fovY * 0.5f));
    float xfov          = yfov * camera_->aspect;
    float idealDistance = std::max(radius / xfov, radius / yfov);

    // Set camera to look at center from +Z direction
    camera_->target   = center;
    camera_->position = center + glm::vec3(0.0f, 0.0f, idealDistance);
    camera_->up       = glm::vec3(0.0f, 1.0f, 0.0f);

    // Update orbit parameters
    orbitDistance_ = idealDistance;
    orbitYaw_      = 0.0f;
    orbitPitch_    = 0.0f;
    updateOrbitPosition();
}


// --------------------------------------------------- //
//  GLFW Callbacks
// --------------------------------------------------- //
void CameraController::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    // Forward to ImGui first
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    // Let ImGui handle input first
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        return;
    }

    auto* controller = sInstance_;
    if (!controller)
    {
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        controller->isOrbiting_ = (action == GLFW_PRESS);
        if (action == GLFW_PRESS)
        {
            controller->firstMouse_ = true;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
    {
        controller->isPanning_ = (action == GLFW_PRESS);
        if (action == GLFW_PRESS)
        {
            controller->firstMouse_ = true;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        controller->isPickingTarget_ = (action == GLFW_PRESS);
        if (action == GLFW_PRESS)
        {
            controller->firstMouse_ = true;
        }
    }
}


void CameraController::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    // Forward to ImGui first
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

    // Let ImGui handle input first
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        return;
    }

    auto* controller = sInstance_;
    if (!controller)
    {
        return;
    }

    float x = static_cast<float>(xpos);
    float y = static_cast<float>(ypos);

    if (controller->firstMouse_)
    {
        controller->lastMouseX_ = x;
        controller->lastMouseY_ = y;
        controller->firstMouse_ = false;
        return;
    }

    float deltaX = x - controller->lastMouseX_;
    float deltaY = y - controller->lastMouseY_;
    controller->lastMouseX_ = x;
    controller->lastMouseY_ = y;

    if (controller->isOrbiting_)
    {
        controller->handleOrbit(deltaX, deltaY);
    }
    else if (controller->isPanning_)
    {
        controller->handlePan(deltaX, deltaY);
    }
    else if (controller->isPickingTarget_)
    {
        controller->handlePickTarget(deltaX, deltaY);
    }
}


void CameraController::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    // Forward to ImGui first
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

    // Let ImGui handle input first
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        return;
    }

    auto* controller = sInstance_;
    if (!controller)
    {
        return;
    }

    controller->handleZoom(static_cast<float>(yoffset));
}


void CameraController::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    // Forward to ImGui first
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    // Let ImGui handle input first
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
    {
        return;
    }

    auto* controller = sInstance_;
    if (!controller)
    {
        return;
    }

    // Reset camera with Home key
    if (key == GLFW_KEY_HOME && action == GLFW_PRESS)
    {
        controller->resetToDefault();
    }
}


// --------------------------------------------------- //
//  Internal Methods
// --------------------------------------------------- //
void CameraController::handleOrbit(float deltaX, float deltaY)
{
    // Drag right -> object rotates right (camera orbits left)
    orbitYaw_   -= deltaX * orbitSensitivity_;
    // RUB (Y-up) + Vulkan Y-flip: drag up -> pitch decreases -> camera below target -> look up
    orbitPitch_ += deltaY * orbitSensitivity_;

    // Clamp pitch to prevent flipping
    orbitPitch_ = std::clamp(orbitPitch_, kMinPitch, kMaxPitch);

    updateOrbitPosition();
}


void CameraController::handlePan(float deltaX, float deltaY)
{
    if (!camera_)
    {
        return;
    }

    glm::vec3 right = camera_->getRight();
    glm::vec3 up    = camera_->getUp();

    // Invert direction: drag right -> view moves right
    float panScale   = orbitDistance_ * panSensitivity_;
    glm::vec3 offset = right * deltaX * panScale - up * deltaY * panScale;

    camera_->position += offset;
    camera_->target   += offset;
}


void CameraController::handleZoom(float delta)
{
    if (!camera_)
    {
        return;
    }

    orbitDistance_ -= delta * zoomSensitivity_ * orbitDistance_ * 0.1f;
    orbitDistance_ = std::clamp(orbitDistance_, kMinDistance, kMaxDistance);
    updateOrbitPosition();
}


void CameraController::handlePickTarget(float deltaX, float deltaY)
{
    if (!camera_)
    {
        return;
    }

    glm::vec3 right = camera_->getRight();
    glm::vec3 up    = camera_->getUp();

    // Move orbit target in screen-space direction, scaled by distance
    float scale      = orbitDistance_ * pickTargetSensitivity_;
    glm::vec3 offset = -right * deltaX * scale + up * deltaY * scale;

    camera_->target += offset;
    updateOrbitPosition();
}


void CameraController::handleKeyboardMovement(float deltaTime)
{
    if (!camera_ || !window_)
    {
        return;
    }

    glm::vec3 forward = camera_->getForward();
    glm::vec3 right   = camera_->getRight();
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    float velocity = movementSpeed_ * deltaTime;
    glm::vec3 movement(0.0f);

    if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS)
    {
        movement += forward * velocity;
    }
    if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS)
    {
        movement -= forward * velocity;
    }
    if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS)
    {
        movement -= right * velocity;
    }
    if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS)
    {
        movement += right * velocity;
    }
    if (glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS)
    {
        movement += worldUp * velocity;
    }
    if (glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS)
    {
        movement -= worldUp * velocity;
    }

    if (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    {
        movement *= 3.0f;
    }

    if (glm::length(movement) > 0.0f)
    {
        camera_->position += movement;
        camera_->target   += movement;
    }
}


void CameraController::updateOrbitPosition()
{
    if (!camera_)
    {
        return;
    }

    // Convert spherical coordinates to Cartesian
    float yawRad   = glm::radians(orbitYaw_);
    float pitchRad = glm::radians(orbitPitch_);

    glm::vec3 offset;
    offset.x = std::cos(pitchRad) * std::sin(yawRad);
    offset.y = std::sin(pitchRad);
    offset.z = std::cos(pitchRad) * std::cos(yawRad);

    camera_->position = camera_->target + offset * orbitDistance_;
}


void CameraController::updateOrbitAnglesFromCamera()
{
    if (!camera_)
    {
        return;
    }

    orbitDistance_ = camera_->getDistanceToTarget();

    // Calculate spherical coordinates from camera position relative to target
    glm::vec3 offset = camera_->position - camera_->target;
    offset = glm::normalize(offset);

    orbitPitch_ = glm::degrees(std::asin(offset.y));
    orbitYaw_   = glm::degrees(std::atan2(offset.x, offset.z));
}

}   // namespace vk3dgrt