// common.glsl - Shared data structures for 3DGRT (GLSL version)
// This file must be kept in sync with src/3dgrt/data.h

#ifndef VK3DGRT_COMMON_GLSL
#define VK3DGRT_COMMON_GLSL


// SH (Spherical Harmonics) - always enabled, degree controlled at runtime via SceneBounds.shDegree
#define MAX_SH_DEGREE 3


// --------------------------------------------------- //
//  Constants
// --------------------------------------------------- //
#define INVALID_PARTICLE_ID    0xFFFFFFFFu
#define INFINITE_DISTANCE      1e20
#define SH_COEFFS_PER_PARTICLE 16

// Compile-time constant for hit array size (matches nvpro PAYLOAD_ARRAY_SIZE)
// Using #define instead of specialization constant to enable loop unrolling
// at GLSL→SPIR-V compile time (nvpro uses Slang compile-time macro for same reason)
#define MAX_HITS_PER_TRACE 18

const float PI      = 3.14159265358979323846;
const float INV_PI  = 0.31830988618379067154;
const float EPSILON = 1e-6;


// --------------------------------------------------- //
//  CameraData - Camera parameters for ray tracing
// --------------------------------------------------- //
struct CameraData
{
    mat4  viewInverse;       // Inverse view matrix
    mat4  projInverse;       // Inverse projection matrix
    vec3  position;          // Camera position in world space
    float fov;               // Field of view
    vec3  forward;           // Camera forward direction
    float aspectRatio;       // Aspect ratio (width/height)
    vec3  right;             // Camera right direction
    float nearPlane;         // Near clipping plane
    vec3  up;                // Camera up direction
    float farPlane;          // Far clipping plane
};


// --------------------------------------------------- //
//  HitArrayPayload - SoA layout for iterative tracing
//  Matches nvpro's HitPayload: separate id[] and dist[] arrays
//  SoA layout is more register-friendly than AoS (struct RayHit[])
// --------------------------------------------------- //
struct HitArrayPayload
{
    uint  id[MAX_HITS_PER_TRACE];    // Particle IDs, sorted by distance (ascending)
    float dist[MAX_HITS_PER_TRACE];  // Hit distances, sorted ascending
};


// --------------------------------------------------- //
//  Transmittance threshold for early ray termination
// --------------------------------------------------- //
const float TRANSMITTANCE_THRESHOLD = 0.01;


// --------------------------------------------------- //
//  Generalized Gaussian Kernel Degree Constants
//  Official coefficient pattern: s = -0.5 / 3^(degree-2)
// --------------------------------------------------- //
#define GAUSSIAN_KERNEL_N1  1   // Standard Gaussian (degree=2): exp(-0.5 * d^2)
#define GAUSSIAN_KERNEL_N2  2   // Generalized Gaussian (degree=4): exp(-1/18 * d^4)
#define DEFAULT_KERNEL_DEGREE  GAUSSIAN_KERNEL_N2


// --------------------------------------------------- //
//  Render Mode Constants
// --------------------------------------------------- //
#define RENDER_MODE_GS      0   // Gaussian Splatting (default)
#define RENDER_MODE_POINT   1   // Point visualization
#define RENDER_MODE_SPLAT   2   // Splat visualization


// --------------------------------------------------- //
//  SceneBounds - AABB for scene extent
// --------------------------------------------------- //
struct SceneBounds
{
    vec3  minBound;       // Minimum corner of scene AABB
    float tMin;           // Minimum ray parameter
    vec3  maxBound;       // Maximum corner of scene AABB
    float tMax;           // Maximum ray parameter
    uint  hasSH;          // 1 if SH coefficients are available, 0 otherwise
    uint  renderMode;     // 0: GS, 1: Point, 2: Splat
    uint  shDegree;       // SH evaluation degree (0=off, 1-3)
    uint  kernelDegree;   // Kernel degree: 1=standard (n=1), 2=generalized (n=2)
};


#endif // VK3DGRT_COMMON_GLSL