#ifndef VK3DGRT_FISHEYE_GLSL
#define VK3DGRT_FISHEYE_GLSL


const int NEWTON_ITERATIONS = 3;


float invertDistortion(float thetaD, float k1, float k2, float k3, float k4)
{
    float theta = thetaD; // Initial guess
    for (int i = 0; i < NEWTON_ITERATIONS; ++i)
    {
        float r2 = theta * theta;
        float r4 = r2 * r2;
        float r6 = r4 * r2;
        float r8 = r4 * r4;

        // Distortion function
        float f = theta * (1.0 + k1 * r2 + k2 * r4 + k3 * r6 + k4 * r8) - thetaD;

        // Derivative of the distortion function
        float df = 1.0 + 3.0 * k1 * r2 + 5.0 * k2 * r4 + 7.0 * k3 * r6 + 9.0 * k4 * r8;

        // Newton-Raphson update
        theta -= f / df;
    }
    return theta;
}


vec3 computeFisheyeRayDirection(vec2 pixelCenter, 
                                vec2 resolution,
                                float fov,
                                float maxAngle,
                                float cx,
                                float cy,
                                float k1,
                                float k2,
                                float k3,
                                float k4)
{
    vec2 center = resolution * 0.5;

    float fx = resolution.x / fov;
    float fy = resolution.y / fov;

    vec2 uvNorm = (pixelCenter - center) / vec2(fx, fy);
    float r = length(uvNorm);

    if (r < EPSILON)
    {
        return vec3(0.0, 0.0, -1.0);
    }

    float theta;
    bool hasDistortion = (k1 != 0.0 || k2 != 0.0 || k3 != 0.0 || k4 != 0.0);
    if (hasDistortion)
    {
        theta = invertDistortion(r, k1, k2, k3, k4);
    }
    else
    {
        theta = r; // No distortion
    }

    float sinTheta = sin(theta);
    vec2  dir2d    = uvNorm / r;

    // Vulkan camera: -Z forward, +Y up
    return vec3(sinTheta * dir2d.x,
                -sinTheta * dir2d.y,
                -cos(theta));
}

#endif // VK3DGRT_FISHEYE_GLSL