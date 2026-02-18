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
//  Camera parameters for ray tracing
// --------------------------------------------------- //
struct Camera
{
    mat4  viewInverse;       // Inverse view matrix
    mat4  projInverse;       // Inverse projection matrix
    float nearPlane;         // Near clipping plane
    float farPlane;          // Far clipping plane
    uint  cameraType;        // 0: Pinhole, 1: Fisheye
    float fisheyeFov;        // FOV in radians (default: PI)
    float fisheyeMaxAngle;   // max angle in radians (default: PI/2)
    float fisheyeCx;         // principal point X offset (pixels)
    float fisheyeCy;         // principal point Y offset (pixels)
    float fisheyeK1;         // radial distortion coefficient 1
    float fisheyeK2;         // radial distortion coefficient 2
    float fisheyeK3;         // radial distortion coefficient 3
    float fisheyeK4;         // radial distortion coefficient 4
    uint  enableDoF;         // 0: disabled, 1: enabled
    float aperture;          // Lens radius
    float focalDistance;     // Focus distance from camera
    uint  frameIndex;        // Accumulation frame counter
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
//  Material Type Constants
// --------------------------------------------------- //
#define MATERIAL_DIFFUSE     0
#define MATERIAL_REFLECTIVE  1


// --------------------------------------------------- //
//  Reflection Constants
// --------------------------------------------------- //
#define MAX_BOUNCES               3
#define MAX_REFLECTION_ITERATIONS 16


// --------------------------------------------------- //
//  MeshHitPayload - Payload for mesh ray hits (std430, 32B)
// --------------------------------------------------- //
struct MeshHitPayload
{
    vec3  normal;
    float hitDist;
    vec3  color;
    uint  materialType;  // MATERIAL_DIFFUSE or MATERIAL_REFLECTIVE
    float reflectivity;
};


// --------------------------------------------------- //
//  MeshMaterialGPU - GPU material structure (std430, 32B)
// --------------------------------------------------- //
struct MeshMaterialGPU
{
    vec3  color;
    uint  materialType;
    float reflectivity;
    float _pad0;
    float _pad1;
    float _pad2;
};


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

    uint enableReflection; // 1 to enable reflections, 0 to disable
    uint maxBounces;       // Maximum number of reflection bounces
    uint meshCount;        // Number of meshes in the scene
    uint _pad0;            // Padding for 16-byte alignment

    // Lighting parameters (must match SceneBoundsUBO layout)
    uint  enableLighting;
    uint  enableSpecular;
    float specularShininess;
    uint  _pad1;

    vec3  lightDir;
    float lightIntensity;

    vec3  lightColor;
    float ambientIntensity;

    vec3  ambientColor;
    uint  _pad2;
};


// --------------------------------------------------- //
//  Lighting - Lambertian diffuse + Blinn-Phong specular
// --------------------------------------------------- //
vec3 computeLighting(vec3 baseColor, vec3 N, vec3 viewDir,
                     vec3 lDir, vec3 lColor, float lIntensity,
                     vec3 aColor, float aIntensity,
                     bool specular, float shininess)
{
    vec3 L = normalize(-lDir);
    vec3 V = normalize(-viewDir);

    // Ambient
    vec3 ambient = aColor * aIntensity * baseColor;

    // Lambertian diffuse
    float NdotL  = max(dot(N, L), 0.0);
    vec3  diffuse = lColor * lIntensity * NdotL * baseColor;

    // Blinn-Phong specular
    vec3 spec = vec3(0.0);
    if (specular && NdotL > 0.0)
    {
        vec3  H     = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        spec = lColor * lIntensity * pow(NdotH, shininess);
    }

    return ambient + diffuse + spec;
}


#endif // VK3DGRT_COMMON_GLSL