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

#include <atomic>
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
    bool                      meshTlasDirty_   = false;
    uint32_t                  meshFrameIndex_  = 0;

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

    // Visualize mode
    uint32_t visualizeMode_ = VISUALIZE_MODE_GS;

    // Render mode
    uint32_t renderMode_ = RENDER_MODE_COLOR;

    // Kernel degree (1=standard, 2=generalized)
    uint32_t kernelDegree_ = DEFAULT_KERNEL_DEGREE;

    // SH degree (0=off, 1-3)
    uint32_t shDegree_ = 0;

    uint32_t cameraType_ = 0;  // 0=pinhole, 1=fisheye

    // Fisheye parameters (radians, pixel offsets)
    float fisheyeFov_      = glm::pi<float>();        // default: PI (180°)
    float fisheyeMaxAngle_ = glm::pi<float>() / 2.f;  // default: PI/2 (90°)
    float fisheyeCx_       = 0.0f;
    float fisheyeCy_       = 0.0f;
    float fisheyeK1_       = 0.0f;
    float fisheyeK2_       = 0.0f;
    float fisheyeK3_       = 0.0f;
    float fisheyeK4_       = 0.0f;

    // Depth of Field parameters
    bool     dofEnabled_       = false;
    float    dofAperture_      = 0.05f;   // Lens radius
    float    dofFocalDistance_ = 5.0f;    // Focus distance from camera
    uint32_t frameIndex_       = 0;       // Accumulation frame counter

    // Shadow parameters
    bool     shadowEnabled_   = false;
    float    shadowIntensity_ = 0.3f;

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

    // Split loading: CPU-only phase (safe to call from background thread)
    bool loadSceneCPU(const std::filesystem::path& plyPath, std::atomic<float>* progress);

    // Split loading: GPU phase, one step per call (must be called from main thread)
    // Returns true when the step succeeds. step range: [0, GPU_LOAD_STEP_COUNT)
    bool loadSceneGPUStep(int step, GLFWwindow* window);

    static constexpr int GPU_LOAD_STEP_COUNT = 5;

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

    void setVisualizeMode(uint32_t mode);

    uint32_t getVisualizeMode() const { return visualizeMode_; }

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

    // Per-mesh material type
    void setMeshMaterial(uint32_t index, MeshMaterialType type);

    // Per-mesh transform (CPU-only update, GPU rebuild happens in recordCommands)
    void updateMeshTransform(uint32_t index, const MeshTransform& transform);

    // Reflection settings
    void setReflectionEnabled(bool enabled);
    bool isReflectionEnabled() const { return reflectionEnabled_; }
    void setMaxBounces(uint32_t bounces);
    uint32_t getMaxBounces() const { return maxBounces_; }

    void setCameraType(uint32_t type) { cameraType_ = type; }
    uint32_t getCameraType() const { return cameraType_; }

    void setFisheyeParams(float fov,
                          float maxAngle,
                          float cx,
                          float cy,
                          float k1,
                          float k2,
                          float k3,
                          float k4);

    // Depth of Field settings
    void     setDoFEnabled(bool enabled);
    bool     isDoFEnabled() const { return dofEnabled_; }
    void     setDoFParams(float aperture, float focalDistance);
    float    getDoFAperture() const { return dofAperture_; }
    float    getDoFFocalDistance() const { return dofFocalDistance_; }
    void     resetAccumulation() { frameIndex_ = 0; }
    uint32_t getFrameIndex() const { return frameIndex_; }

    // Shadow settings
    void  setShadowEnabled(bool enabled);
    bool  isShadowEnabled() const { return shadowEnabled_; }
    void  setShadowIntensity(float intensity);
    float getShadowIntensity() const { return shadowIntensity_; }

private:
    void computeSceneBounds();
    CameraUBO buildCameraUBO() const;
    SceneBoundsUBO buildSceneBoundsUBO() const;
    bool rebuildMeshResources();
    std::vector<MeshTLASInstance> buildMeshTLASInstances() const;
};

}   // namespace vk3dgrt

#endif // GRT_SCENE_H