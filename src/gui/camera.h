#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>


struct GLFWwindow;


namespace vk3dgrt {

class Camera
{
public:
    // Camera position and orientation
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f);
    glm::vec3 target   = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up       = glm::vec3(0.0f, 1.0f, 0.0f);

    // Projection parameters
    float fovY   = 60.0f;           // Vertical field of view in degrees
    float aspect = 16.0f / 9.0f;    // Aspect ratio (width / height)
    float zNear  = 0.1f;            // Near clipping plane
    float zFar   = 2000.0f;         // Far clipping plane (matching nvpro)

    Camera()  = default;
    ~Camera() = default;

    // Matrix getters
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::mat4 getViewProjectionMatrix() const;
    glm::mat4 getInverseViewMatrix() const;
    glm::mat4 getInverseProjectionMatrix() const;
    glm::mat4 getInverseViewProjectionMatrix() const;

    // Direction vectors
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    // Update aspect ratio (call when window resizes)
    void setAspect(float newAspect);
    void setAspect(uint32_t width, uint32_t height);

    // Look at a specific target
    void lookAt(const glm::vec3& newTarget);
    void lookAt(const glm::vec3& newPosition, const glm::vec3& newTarget, const glm::vec3& newUp);

    // Set position maintaining target
    void setPosition(const glm::vec3& newPosition);

    // Get distance to target
    float getDistanceToTarget() const;
};


class CameraController
{
    GLFWwindow* window_ = nullptr;
    Camera*     camera_ = nullptr;

    // Mouse state
    bool  isOrbiting_       = false;
    bool  isPanning_        = false;
    bool  isPickingTarget_  = false;
    float lastMouseX_       = 0.0f;
    float lastMouseY_       = 0.0f;
    bool  firstMouse_       = true;

    // Orbit parameters
    float orbitYaw_      = 0.0f;     // Horizontal angle in degrees
    float orbitPitch_    = 0.0f;     // Vertical angle in degrees
    float orbitDistance_ = 5.0f;     // Distance from target

    // Sensitivity settings
    float orbitSensitivity_      = 0.5f;
    float panSensitivity_        = 0.01f;
    float zoomSensitivity_       = 0.5f;
    float pickTargetSensitivity_ = 0.003f;
    float movementSpeed_         = 3.0f;

    // Pitch limits (to prevent gimbal lock)
    static constexpr float kMinPitch = -89.0f;
    static constexpr float kMaxPitch = 89.0f;

    // Zoom limits
    static constexpr float kMinDistance = 0.1f;
    static constexpr float kMaxDistance = 1000.0f;

    // Static instance for GLFW callback access (avoids glfwSetWindowUserPointer conflict)
    static CameraController* sInstance_;

public:
    CameraController()  = default;
    ~CameraController() = default;

    // Initialization
    void initialize(GLFWwindow* window, Camera* camera);
    void shutdown();

    // Per-frame update (reserved for future use)
    void update(float deltaTime);

    // Sensitivity settings
    void setOrbitSensitivity(float sensitivity) { orbitSensitivity_ = sensitivity; }
    void setPanSensitivity(float sensitivity)   { panSensitivity_ = sensitivity; }
    void setZoomSensitivity(float sensitivity)  { zoomSensitivity_ = sensitivity; }

    float getOrbitSensitivity() const { return orbitSensitivity_; }
    float getPanSensitivity() const   { return panSensitivity_; }
    float getZoomSensitivity() const  { return zoomSensitivity_; }

    // Check if controller is capturing input
    bool isCapturingInput() const { return isOrbiting_ || isPanning_ || isPickingTarget_; }

    // Reset camera to default position
    void resetToDefault();

    // Focus on scene bounds
    void focusOnBounds(const glm::vec3& minBound, const glm::vec3& maxBound);

private:
    // GLFW callbacks
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    // Internal methods
    void handleOrbit(float deltaX, float deltaY);
    void handlePan(float deltaX, float deltaY);
    void handleZoom(float delta);
    void handlePickTarget(float deltaX, float deltaY);
    void handleKeyboardMovement(float deltaTime);
    void updateOrbitPosition();
    void updateOrbitAnglesFromCamera();
};

}   // namespace vk3dgrt

#endif // CAMERA_H