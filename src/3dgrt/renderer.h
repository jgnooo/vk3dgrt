#ifndef RENDERER_H
#define RENDERER_H

#include "data.h"
#include "buffers.h"
#include "tlas.h"
#include "blas.h"
#include "mesh-buffers.h"
#include "mesh-tlas.h"
#include "rt-descriptor-set.h"

#include "vulkan/vkcontext.h"
#include "vulkan/vkpipeline.h"
#include "vulkan/vkimage.h"
#include "vulkan/vkbuffer.h"

#include <glm/glm.hpp>

#include <string>


namespace vk3dgrt {

struct CameraPushConstants
{
    glm::mat4 viewInverse;       // Inverse view matrix           - 64 bytes
    glm::mat4 projInverse;       // Inverse projection matrix     - 64 bytes
    glm::vec3 position;          // Camera position in world space
    float     fov;               // Field of view
    glm::vec3 forward;           // Camera forward direction
    float     aspectRatio;       // Aspect ratio (width/height)
    glm::vec3 right;             // Camera right direction
    float     nearPlane;         // Near clipping plane
    glm::vec3 up;                // Camera up direction
    float     farPlane;          // Far clipping plane

    // SoA Buffer Device Addresses for bindless buffer access
    VkDeviceAddress positionBufferAddress   = 0;  // vec3[] positions BDA
    VkDeviceAddress colorBufferAddress      = 0;  // vec4[] DC color (RGB) + opacity (A) BDA
    VkDeviceAddress quaternionBufferAddress = 0;  // vec4[] quaternions BDA
    VkDeviceAddress scaleBufferAddress      = 0;  // vec3[] scales BDA
    VkDeviceAddress shBufferAddress         = 0;  // SH coefficients BDA
};


static_assert(sizeof(CameraPushConstants) == 232, "CameraPushConstants must match GLSL PushConstants size (192 camera + 40 BDA)");


// Alias for backward compatibility
using CameraUBO = CameraPushConstants;


constexpr uint32_t RENDER_MODE_GS       = 0;  // Gaussian Splatting (default)
constexpr uint32_t RENDER_MODE_POINT    = 1;  // Point visualization
constexpr uint32_t RENDER_MODE_SPLAT    = 2;  // Splat visualization


struct SceneBoundsUBO
{
    // --- Existing fields (48 bytes) ---
    glm::vec3 minBound;          // Minimum corner of scene AABB
    float     tMin;              // Minimum ray parameter
    glm::vec3 maxBound;          // Maximum corner of scene AABB
    float     tMax;              // Maximum ray parameter
    uint32_t  hasSH;             // 1 if SH coefficients are available, 0 otherwise
    uint32_t  renderMode;        // 0: GS, 1: Point, 2: Splat
    uint32_t  shDegree;          // SH evaluation degree (0=off, 1-3)
    uint32_t  kernelDegree;      // Kernel degree: 1=standard (n=1), 2=generalized (n=2)

    // --- Reflection parameters (16 bytes) ---
    uint32_t  enableReflection = 0;   // 0: disabled, 1: enabled
    uint32_t  maxBounces       = 1;   // Maximum reflection bounces (1-3)
    uint32_t  meshCount        = 0;   // Number of inserted meshes
    uint32_t  _pad0            = 0;

    // --- Lighting parameters (64 bytes) ---
    uint32_t  enableLighting    = 0;      // 0: disabled, 1: enabled
    uint32_t  enableSpecular    = 0;      // 0: disabled, 1: enabled
    float     specularShininess = 32.0f;  // Blinn-Phong shininess exponent
    uint32_t  _pad1             = 0;

    glm::vec3 lightDir          = glm::vec3(0.0f, -1.0f, 0.0f);  // Normalized direction
    float     lightIntensity    = 1.0f;

    glm::vec3 lightColor        = glm::vec3(1.0f);
    float     ambientIntensity  = 0.2f;

    glm::vec3 ambientColor      = glm::vec3(1.0f);
    uint32_t  _pad2             = 0;
};


static_assert(sizeof(SceneBoundsUBO) == 128, "SceneBoundsUBO must match GLSL SceneBounds size");


class Renderer
{
    bool       initialized  = false;
    VkContext* ctx          = nullptr;

    AllocatedImage outputImage;

    CameraPushConstants cameraPushConstants{};

    AllocatedBuffer sceneBoundsUBO;

    RayTraceDescriptorSet rtDescriptorSet;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;

    ShaderModule raygenShader;
    ShaderModule missShader;
    ShaderModule closestHitShader;
    ShaderModule anyHitShader;
    ShaderModule meshClosestHitShader;

    ShaderBindingTable sbt;

    RayTracingPipelineBuilder pipelineBuilder;

public:
    Renderer()  = default;
    ~Renderer() = default;

    // Disable copy, allow move
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&)                 = default;
    Renderer& operator=(Renderer&&)      = default;

    bool initialize(VkContext* context,
                    uint32_t width,
                    uint32_t height,
                    const std::string& shaderPath = "shaders/");

    void cleanup(VkContext* context);

    bool updateDescriptors(const TLAS& tlas,
                           const GaussianParticleBuffers& gaussianParticleBuffers,
                           const MeshTLAS* meshTlas       = nullptr,
                           const MeshBuffers* meshBuffers = nullptr);
    void updateCamera(const CameraPushConstants& camera);
    void updateSceneBounds(const SceneBoundsUBO& bounds);

    void recordRayTrace(VkCommandBuffer cmdBuffer);

    void copyToSwapchain(VkCommandBuffer cmdBuffer,
                         VkImage dstImage,
                         VkExtent2D dstExtent);

    bool resize(uint32_t width, uint32_t height);

    bool isInitialized() const { return initialized; }

    VkExtent2D getOutputExtent() const { return outputImage.extent; }

    const AllocatedImage& getOutputImage() const { return outputImage; }

    void markDescriptorsDirty() { rtDescriptorSet.markDirty(); }

    bool areDescriptorsDirty() const { return rtDescriptorSet.isDirty(); }

    int32_t getHitsPerTrace() const { return MAX_HITS_PER_TRACE; }

private:
    bool createUniformBuffers();
    bool createOutputImage(uint32_t width, uint32_t height);
    bool loadShaders(const std::string& shaderPath);
    bool createPipeline();
    bool createShaderBindingTable();
};

}   // namespace vk3dgrt

#endif // RENDERER_H