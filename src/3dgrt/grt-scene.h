#ifndef GRT_SCENE_H
#define GRT_SCENE_H

#include "data.h"
#include "loader.h"
#include "buffers.h"
#include "renderer.h"
#include "blas.h"
#include "tlas.h"
#include "mesh-data.h"
#include "mesh-buffers.h"
#include "mesh-blas.h"
#include "mesh-tlas.h"
#include "gui/camera.h"

#include "scene.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>


// Forward declarations
struct GLFWwindow;


namespace vk3dgrt {

class GRTScene : public Scene
{
    bool initialized = false;
    bool dataLoaded  = false;

    // Resource provider
    VkProvider* provider_ = nullptr;

    // Scene data
    GaussianParticleData    gaussianParticleData;
    GaussianParticleBuffers gaussianParticleBuffers;
    
    BLAS  blas;
    TLAS  tlas;

    // Mesh resources (Dual TLAS: separate from Gaussian TLAS)
    std::vector<MeshInstance> meshInstances_;
    MeshBuffers               meshBuffers_;
    MeshBLAS                  meshBlas_;
    MeshTLAS                  meshTlas_;

    // Reflection state
    bool     reflectionEnabled_ = false;
    uint32_t maxBounces_        = 1;

    // Renderer
    Renderer renderer;

    // Camera system
    Camera           camera;
    CameraController cameraController;

    // Scene bounds
    glm::vec3 minBound = glm::vec3(0.0f);
    glm::vec3 maxBound = glm::vec3(0.0f);

    // Render mode
    uint32_t renderMode_ = RENDER_MODE_GS;

    // Kernel degree (1=standard, 2=generalized)
    uint32_t kernelDegree_ = DEFAULT_KERNEL_DEGREE;

    // SH degree (0=off, 1-3)
    uint32_t shDegree_ = 0;

public:
    GRTScene()  = default;
    ~GRTScene() override = default;

    // Disable copy, allow move
    GRTScene(const GRTScene&)            = delete;
    GRTScene& operator=(const GRTScene&) = delete;
    GRTScene(GRTScene&&)                 = default;
    GRTScene& operator=(GRTScene&&)      = default;

    bool initialize(VkProvider* provider) override;

    void cleanup() override;

    bool isInitialized() const override { return initialized; }

    void recordCommands(VkCommandBuffer cmd) override;

    void copyToSwapchain(VkCommandBuffer cmd,
                         VkImage dstImage,
                         VkExtent2D dstExtent) override;

    void update(float deltaTime) override;

    void onResize(uint32_t width, uint32_t height) override;

    bool loadScene(const std::filesystem::path& plyPath, GLFWwindow* window);

    bool initializeEmpty(GLFWwindow* window);

    bool hasData() const { return dataLoaded; }

    Camera& getCamera() { return camera; }
    const Camera& getCamera() const { return camera; }

    CameraController& getCameraController() { return cameraController; }
    const CameraController& getCameraController() const { return cameraController; }

    void getSceneBounds(glm::vec3& outMin, glm::vec3& outMax) const
    {
        outMin = minBound;
        outMax = maxBound;
    }

    uint32_t getParticleCount() const
    {
        return static_cast<uint32_t>(gaussianParticleData.getParticleCount());
    }

    const Renderer& getRenderer() const { return renderer; }

    void setRenderMode(uint32_t mode);

    uint32_t getRenderMode() const { return renderMode_; }

    void setSHDegree(uint32_t degree);

    uint32_t getSHDegree() const { return shDegree_; }

    bool hasSHData() const { return gaussianParticleBuffers.hasSHCoefficients(); }

    // Mesh management
    bool addMesh(MeshPreset preset,
                 const glm::vec3& position = glm::vec3(0.0f),
                 const glm::vec3& scale    = glm::vec3(1.0f));
    bool removeMesh(uint32_t index);
    uint32_t getMeshCount() const { return static_cast<uint32_t>(meshInstances_.size()); }
    const std::vector<MeshInstance>& getMeshInstances() const { return meshInstances_; }

    // Reflection settings
    void setReflectionEnabled(bool enabled);
    bool isReflectionEnabled() const { return reflectionEnabled_; }
    void setMaxBounces(uint32_t bounces);
    uint32_t getMaxBounces() const { return maxBounces_; }

private:
    void computeSceneBounds();
    CameraUBO buildCameraUBO() const;
    SceneBoundsUBO buildSceneBoundsUBO() const;
    bool rebuildMeshResources();
    std::vector<MeshTLASInstance> buildMeshTLASInstances() const;
};

}   // namespace vk3dgrt

#endif // GRT_SCENE_H