#ifndef VK3DGRT_DOF_GLSL
#define VK3DGRT_DOF_GLSL


uint pcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}


float randomFloat(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed) / 4294967296.0;
}


// Uniform sampling on unit disk using concentric mapping
// Returns (x, y) in [-1, 1] with x^2 + y^2 <= 1
vec2 sampleDisk(inout uint seed)
{
    float u1 = randomFloat(seed) * 2.0 - 1.0;
    float u2 = randomFloat(seed) * 2.0 - 1.0;
    
    if (u1 == 0.0 && u2 == 0.0)
    {
        return vec2(0.0);
    }

    // Concentric disk mapping (Shirley & Chiu, 1997)
    // Maps square to disk with low distortion
    float r, theta;
    if (abs(u1) > abs(u2))
    {
        r = u1;
        theta = (PI / 4.0) * (u2 / u1);
    }
    else
    {
        r = u2;
        theta = (PI / 2.0) - (PI / 4.0) * (u1 / u2);
    }

    return vec2(r * cos(theta), r * sin(theta));
}


// Compute DoF ray origin and direction using Thin Lens model
// pinOrigin:    original pinhole camera position
// pinDirection: original pinhole ray direction (normalized)
// aperture:     lens radius (0 = pinhole, no blur)
// focalDist:    distance to focal plane along view direction
// viewInverse:  camera-to-world transform
// seed:         random seed (updated in-place)
void computeDoFRay(vec3 pinOrigin, 
                   vec3 pinDirection, 
                   float aperture, 
                   float focalDist, 
                   mat4 viewInverse, 
                   inout uint seed, 
                   out vec3 doFRayOrigin, 
                   out vec3 doFRayDirection)
{
    vec3 focusPoint = pinOrigin + pinDirection * focalDist;

    vec2 diskSample = sampleDisk(seed);
    vec3 lensOffset = vec3(diskSample * aperture, 0.0);

    vec3 worldOffset = (viewInverse * vec4(lensOffset, 0.0)).xyz;

    doFRayOrigin    = pinOrigin + worldOffset;
    doFRayDirection = normalize(focusPoint - doFRayOrigin);
}

#endif // VK3DGRT_DOF_GLSL