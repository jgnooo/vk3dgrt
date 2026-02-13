// math.glsl - Mathematical utility functions for 3DGRT shaders

#ifndef VK3DGRT_MATH_GLSL
#define VK3DGRT_MATH_GLSL

#include "common.glsl"


// --------------------------------------------------- //
//  Quaternion Operations
// --------------------------------------------------- //

mat3 quaternionWXYZToMatrix(vec4 q)
{
    float w = q.x;  // W component
    float x = q.y;  // X component
    float y = q.z;  // Y component
    float z = q.w;  // Z component

    float x2 = x + x;
    float y2 = y + y;
    float z2 = z + z;

    float xx = x * x2;
    float xy = x * y2;
    float xz = x * z2;
    float yy = y * y2;
    float yz = y * z2;
    float zz = z * z2;
    float wx = w * x2;
    float wy = w * y2;
    float wz = w * z2;

    return mat3(
        1.0 - (yy + zz), xy + wz,         xz - wy,
        xy - wz,         1.0 - (xx + zz), yz + wx,
        xz + wy,         yz - wx,         1.0 - (xx + yy)
    );
}


mat3 quaternionXYZWToMatrix(vec4 q)
{
    float x = q.x;
    float y = q.y;
    float z = q.z;
    float w = q.w;

    float x2 = x + x;
    float y2 = y + y;
    float z2 = z + z;

    float xx = x * x2;
    float xy = x * y2;
    float xz = x * z2;
    float yy = y * y2;
    float yz = y * z2;
    float zz = z * z2;
    float wx = w * x2;
    float wy = w * y2;
    float wz = w * z2;

    return mat3(
        1.0 - (yy + zz), xy + wz,         xz - wy,
        xy - wz,         1.0 - (xx + zz), yz + wx,
        xz + wy,         yz - wx,         1.0 - (xx + yy)
    );
}


mat3 quaternionToMatrix(vec4 q)
{
    return quaternionWXYZToMatrix(q);
}


// --------------------------------------------------- //
//  Covariance Matrix Operations
// --------------------------------------------------- //

mat3 computeCovarianceMatrix(vec3 scale, vec4 rotation)
{
    mat3 R = quaternionWXYZToMatrix(rotation);
    mat3 S = mat3(
        scale.x, 0.0, 0.0,
        0.0, scale.y, 0.0,
        0.0, 0.0, scale.z
    );

    mat3 RS = R * S;
    return RS * transpose(RS);
}


mat3 computeInvCovarianceMatrix(vec3 scale, vec4 rotation)
{
    mat3 R = quaternionWXYZToMatrix(rotation);

    // Inverse scale (1/s for each axis)
    vec3 invScale = 1.0 / max(scale, vec3(1e-8));

    mat3 invS = mat3(
        invScale.x, 0.0, 0.0,
        0.0, invScale.y, 0.0,
        0.0, 0.0, invScale.z
    );

    // Inv(Cov) = R * Inv(S)^2 * R^T
    mat3 RinvS = R * invS;
    return RinvS * transpose(RinvS);
}


// --------------------------------------------------- //
//  Gaussian Evaluation
// --------------------------------------------------- //

float evaluateGaussian(vec3 point, vec3 center, mat3 invCovariance)
{
    vec3 diff = point - center;
    float exponent = -0.5 * dot(diff, invCovariance * diff);
    return exp(exponent);
}


float evaluateGaussianWithOpacity(vec3 point, vec3 center, mat3 invCovariance, float opacity)
{
    return opacity * evaluateGaussian(point, center, invCovariance);
}


// --------------------------------------------------- //
//  Vector Utilities
// --------------------------------------------------- //

vec3 safeNormalize(vec3 v)
{
    float len = length(v);
    return len > 1e-8 ? v / len : vec3(0.0);
}


vec3 perpendicular(vec3 v)
{
    if (abs(v.x) < 0.9)
    {
        return normalize(cross(v, vec3(1.0, 0.0, 0.0)));
    }
    return normalize(cross(v, vec3(0.0, 1.0, 0.0)));
}


// --------------------------------------------------- //
//  Adaptive Clamping (Opacity-based Scale)
// --------------------------------------------------- //

// Minimum alpha threshold for kernel scale computation
#define DEFAULT_MIN_ALPHA (1.0 / 255.0)

float computeKernelScale(float opacity, float minAlpha, int kernelDegree)
{
    float clampedOpacity = max(opacity, minAlpha);
    float logRatio = log(clampedOpacity / minAlpha);

    if (kernelDegree == GAUSSIAN_KERNEL_N1)
    {
        // n=1 (degree=2): r = sqrt(2 * log(opacity / minAlpha))
        return sqrt(2.0 * logRatio);
    }
    else
    {
        // n=2 (degree=4): r = (18 * log(opacity / minAlpha))^(1/4)
        return pow(18.0 * logRatio, 0.25);
    }
}


float computeKernelScale(float opacity)
{
    return computeKernelScale(opacity, DEFAULT_MIN_ALPHA, DEFAULT_KERNEL_DEGREE);
}


mat4 computeInstanceTransformMatrix(
    vec3 position,
    vec4 quaternion,
    vec3 scale,
    float kernelScale)
{
    // Build rotation matrix from quaternion
    mat3 R = quaternionWXYZToMatrix(quaternion);

    // Scale matrix with kernel scale applied
    vec3 scaledScale = scale * kernelScale;
    mat3 S = mat3(
        scaledScale.x, 0.0, 0.0,
        0.0, scaledScale.y, 0.0,
        0.0, 0.0, scaledScale.z
    );

    // Combine R * S into upper 3x3
    mat3 RS = R * S;

    // Build 4x4 matrix with translation
    mat4 transform = mat4(1.0);
    transform[0] = vec4(RS[0], 0.0);
    transform[1] = vec4(RS[1], 0.0);
    transform[2] = vec4(RS[2], 0.0);
    transform[3] = vec4(position, 1.0);

    return transform;
}

#endif // VK3DGRT_MATH_GLSL