#ifndef DATA_H
#define DATA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>


namespace vk3dgrt {

// General constants
constexpr uint32_t INVALID_PARTICLE_ID    = 0xFFFFFFFF;
constexpr float    INFINITE_DISTANCE      = 1e20f;
constexpr uint32_t MAX_HITS_PER_TRACE     = 18;
constexpr uint32_t SH_COEFFS_PER_PARTICLE = 16;

// SH degree 0 constant: C0 = 1 / (2 * sqrt(pi))
// Used for pre-computing DC color: color = SH_C0 * shDC + 0.5
constexpr float SH_C0 = 0.28209479177387814f;

// Adaptive Clamping constants
constexpr float    DEFAULT_MIN_ALPHA = 1.0f / 255.0f;

// Generalized Gaussian kernel degree constants (matching shader)
// Reference: 3DGRT Paper (SIGGRAPH Asia 2024), Section 4.2
constexpr int      GAUSSIAN_KERNEL_N1     = 1;                   // Standard Gaussian: exp(-0.5 * d²)
constexpr int      GAUSSIAN_KERNEL_N2     = 2;                   // Generalized Gaussian: exp(-0.5 * d⁴)
constexpr int      DEFAULT_KERNEL_DEGREE  = GAUSSIAN_KERNEL_N2;  // Use n=2 for 2x performance


struct GaussianParticle
{
    glm::vec3 position   = glm::vec3(0.0f);                     // 3D position (x, y, z)     - 12 bytes
    float     opacity    = 0.0f;                                // opacity (0~1)             -  4 bytes
    glm::vec4 quaternion = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);   // rotation (W, X, Y, Z)     - 16 bytes
    glm::vec3 scale      = glm::vec3(1.0f);                     // scale per axis            - 12 bytes
    float     padding    = 0.0f;                                // 16-byte alignment padding -  4 bytes
};


// Compile-time verification that C++ struct layout matches GPU std430 layout exactly.
// Any mismatch (e.g. compiler-inserted padding) will cause a build error,
// preventing silent data corruption when uploading to GPU buffers.
static_assert(sizeof(GaussianParticle)  == 48, "GaussianParticle must be 48 bytes for GPU alignment");
static_assert(alignof(GaussianParticle) == 4,  "GaussianParticle must have 4-byte alignment");

static_assert(offsetof(GaussianParticle, position)   == 0,  "GaussianParticle::position offset must be 0");
static_assert(offsetof(GaussianParticle, opacity)    == 12, "GaussianParticle::opacity offset must be 12");
static_assert(offsetof(GaussianParticle, quaternion) == 16, "GaussianParticle::quaternion offset must be 16");
static_assert(offsetof(GaussianParticle, scale)      == 32, "GaussianParticle::scale offset must be 32");
static_assert(offsetof(GaussianParticle, padding)    == 44, "GaussianParticle::padding offset must be 44");


struct RayHit
{
    uint32_t particleId = INVALID_PARTICLE_ID;  // particle index (0xFFFFFFFF = invalid)
    float    distance   = INFINITE_DISTANCE;    // hit distance (1e20 = infinite)
};


static_assert(sizeof(RayHit) == 8, "RayHit must be 8 bytes");

static_assert(offsetof(RayHit, particleId) == 0, "RayHit::particleId offset must be 0");
static_assert(offsetof(RayHit, distance)   == 4, "RayHit::distance offset must be 4");


struct GaussianData
{
    std::vector<GaussianParticle> particles;

    // SH coefficients storage
    // Full degree 3 SH: 16 coefficients per channel, 48 floats per particle
    // Storage layout: [particle0_sh0_r, particle0_sh0_g, particle0_sh0_b, particle0_sh1_r, ...]
    uint32_t               shDegree = 0;
    std::vector<glm::vec3> shCoeffsDC;       // DC term (1 per particle) - degree 0
    std::vector<glm::vec3> shCoeffsRest;     // Higher order: 15 vec3 per particle (degree 1-3)
    std::vector<float>     shCoeffsFull;     // Packed full SH: 48 floats per particle

    size_t getParticleCount() const
    {
        return particles.size();
    }


    size_t getDataSizeBytes() const
    {
        return particles.size() * sizeof(GaussianParticle);
    }


    size_t getSHDataSizeBytes() const
    {
        // 16 vec3 per particle = 48 floats = 192 bytes per particle
        return shCoeffsFull.size() * sizeof(float);
    }


    bool hasFullSH() const
    {
        return shDegree >= 3 && !shCoeffsFull.empty();
    }


    void clear()
    {
        particles.clear();
        shCoeffsDC.clear();
        shCoeffsRest.clear();
        shCoeffsFull.clear();
        shDegree = 0;
    }
};


inline glm::mat3 quaternionToMatrix(const glm::vec4& q)
{
    const float w = q.x;  // W component
    const float x = q.y;  // X component
    const float y = q.z;  // Y component
    const float z = q.w;  // Z component

    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;

    const float xx = x * x2;
    const float xy = x * y2;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float zz = z * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float wz = w * z2;

    return glm::mat3(
        1.0f - (yy + zz), xy + wz,          xz - wy,
        xy - wz,          1.0f - (xx + zz), yz + wx,
        xz + wy,          yz - wx,          1.0f - (xx + yy)
    );
}


inline bool isValidHit(const RayHit& hit)
{
    return hit.particleId != INVALID_PARTICLE_ID;
}


inline RayHit createMissHit()
{
    return RayHit{ INVALID_PARTICLE_ID, INFINITE_DISTANCE };
}


// Compute kernel scale for adaptive bounding (generalized Gaussian)
inline float computeKernelScale(float opacity,
                                float minAlpha     = DEFAULT_MIN_ALPHA,
                                int   kernelDegree = DEFAULT_KERNEL_DEGREE)
{
    // Clamp opacity to valid range [minAlpha, 1.0]
    const float clampedOpacity = std::max(opacity, minAlpha);

    // log(opacity / minAlpha) - base term for all kernel degrees
    const float logRatio = std::logf(clampedOpacity / minAlpha);

    if (kernelDegree == GAUSSIAN_KERNEL_N1)
    {
        // n=1 (degree=2, s=-0.5): exp(-0.5 * d²) = minAlpha/opacity
        // d = sqrt(2 * log(opacity / minAlpha))
        // When opacity == 1.0, minAlpha=1/255: r ≈ 3.33
        return std::sqrtf(2.0f * logRatio);
    }
    else // GAUSSIAN_KERNEL_N2 or default
    {
        // n=2 (degree=4, s=-1/18): exp(-1/18 * d⁴) = minAlpha/opacity
        // d⁴ = 18 * log(opacity / minAlpha)
        // d  = (18 * log(opacity / minAlpha))^(1/4)
        // When opacity == 1.0, minAlpha=1/255: r ≈ 3.16
        return std::powf(18.0f * logRatio, 0.25f);
    }
}


inline glm::mat4 computeInstanceTransform(const glm::vec3& position,
                                          const glm::vec4& quaternion,
                                          const glm::vec3& scale,
                                          float kernelScale)
{
    // Build T * R * S transform
    const glm::quat rotation(quaternion.x, quaternion.y, quaternion.z, quaternion.w);

    const glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
    const glm::mat4 rotationMatrix    = glm::mat4_cast(rotation);
    const glm::mat4 scaleMatrix       = glm::scale(glm::mat4(1.0f), scale * kernelScale);

    return translationMatrix * rotationMatrix * scaleMatrix;
}


inline glm::mat4 computeInstanceTransform(const GaussianParticle& particle,
                                          float minAlpha     = DEFAULT_MIN_ALPHA,
                                          int   kernelDegree = DEFAULT_KERNEL_DEGREE)
{
    const float kernelScale = computeKernelScale(particle.opacity, minAlpha, kernelDegree);
    return computeInstanceTransform(
        particle.position,
        particle.quaternion,
        particle.scale,
        kernelScale
    );
}


/**
 * VkTransformMatrixKHR is a row-major 3x4 matrix used by Vulkan ray tracing.
 * glm uses column-major storage, so we need to transpose and extract the
 * upper 3x4 portion.
 */
inline VkTransformMatrixKHR toVkTransformMatrix(const glm::mat4& mat)
{
    VkTransformMatrixKHR vkTransform{};

    // glm is column-major: mat[col][row]
    // VkTransformMatrixKHR is row-major: matrix[row][col]
    // We need to transpose while copying

    // Row 0
    vkTransform.matrix[0][0] = mat[0][0];  // col 0, row 0
    vkTransform.matrix[0][1] = mat[1][0];  // col 1, row 0
    vkTransform.matrix[0][2] = mat[2][0];  // col 2, row 0
    vkTransform.matrix[0][3] = mat[3][0];  // col 3, row 0 (translation X)

    // Row 1
    vkTransform.matrix[1][0] = mat[0][1];  // col 0, row 1
    vkTransform.matrix[1][1] = mat[1][1];  // col 1, row 1
    vkTransform.matrix[1][2] = mat[2][1];  // col 2, row 1
    vkTransform.matrix[1][3] = mat[3][1];  // col 3, row 1 (translation Y)

    // Row 2
    vkTransform.matrix[2][0] = mat[0][2];  // col 0, row 2
    vkTransform.matrix[2][1] = mat[1][2];  // col 1, row 2
    vkTransform.matrix[2][2] = mat[2][2];  // col 2, row 2
    vkTransform.matrix[2][3] = mat[3][2];  // col 3, row 2 (translation Z)

    return vkTransform;
}


inline VkTransformMatrixKHR computeVkInstanceTransform(const GaussianParticle& particle,
                                                       float minAlpha     = DEFAULT_MIN_ALPHA,
                                                       int   kernelDegree = DEFAULT_KERNEL_DEGREE)
{
    const glm::mat4 transform = computeInstanceTransform(particle, minAlpha, kernelDegree);
    return toVkTransformMatrix(transform);
}

}   // namespace vk3dgrt

#endif // DATA_H