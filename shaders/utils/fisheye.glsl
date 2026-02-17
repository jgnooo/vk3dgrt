#ifndef VK3DGRT_FISHEYE_GLSL
#define VK3DGRT_FISHEYE_GLSL

const float FISHEYE_FOV       = PI;
const float FISHEYE_MAX_ANGLE = FISHEYE_FOV * 0.5;


vec3 computeFisheyeRayDirection(vec2 pixelCenter, vec2 resolution)
{
    vec2 center = resolution * 0.5;

    float fx = resolution.x / FISHEYE_FOV;
    float fy = resolution.y / FISHEYE_FOV;

    vec2 uvNorm = (pixelCenter - center) / vec2(fx, fy);
    float r = length(uvNorm);

    if (r < EPSILON)
    {
        return vec3(0.0, 0.0, -1.0);
    }

    float theta = r;
    if (theta > FISHEYE_MAX_ANGLE)
    {
        return vec3(0.0);
    }

    float sinTheta = sin(theta);
    vec2  dir2d    = uvNorm / r;

    // Vulkan camera: -Z forward, +Y up
    return vec3(sinTheta * dir2d.x,
                -sinTheta * dir2d.y,
                -cos(theta));
}

#endif // VK3DGRT_FISHEYE_GLSL