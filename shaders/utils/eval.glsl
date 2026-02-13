// eval.glsl - Core Gaussian rendering functions for 3DGRT
// Implements particle response and grayDist computation
// Based on the official 3DGRT implementation (Section 3.5)

#ifndef VK3DGRT_EVAL_GLSL
#define VK3DGRT_EVAL_GLSL

#include "math.glsl"


// --------------------------------------------------- //
//  Generalized Gaussian Kernel Constants
//
//  Reference: 3DGRT Paper (SIGGRAPH Asia 2024), Section 4.2
//  "Generalized Gaussian (n=2) provides 2x framerate improvement"
//
//  Official implementation (gaussianParticles.slang) uses kernel "degree"
//  with coefficient pattern: s = -0.5 / 3^(degree-2)
//
//  - degree=2 (Standard):    s=-0.5,   exp(-0.5 * d²)     - Smooth falloff
//  - degree=4 (Generalized): s=-1/18,  exp(-1/18 * d⁴)    - Default in official
//
//  Performance comparison (from paper):
//    degree=2: 143 FPS, PSNR 23.03
//    degree=4: 277 FPS, PSNR 22.68 (2x speedup, minimal quality loss)
// --------------------------------------------------- //

#define GAUSSIAN_KERNEL_DEGREE_2  GAUSSIAN_KERNEL_N1
#define GAUSSIAN_KERNEL_DEGREE_4  GAUSSIAN_KERNEL_N2


// --------------------------------------------------- //
//  Particle Response Function
// --------------------------------------------------- //

float particleResponse(float grayDist, int n)
{
    if (n == GAUSSIAN_KERNEL_N1)
    {
        return exp(-0.5 * grayDist);
    }
    else
    {
        const float s = -0.0555555555556;  // -1/18
        return exp(s * grayDist * grayDist);
    }
}


// --------------------------------------------------- //
//  Gray Distance (Mahalanobis Distance) Computation
// --------------------------------------------------- //

float computeGrayDist(
    vec3 hitPoint,
    vec3 particlePosition,
    mat3 rotationMatrix,
    vec3 scale)
{
    // Transform hit point to particle's local coordinate system
    // R^T rotates from world space to local space
    vec3 localHit = transpose(rotationMatrix) * (hitPoint - particlePosition);

    // Normalize by scale (avoid division by zero)
    vec3 scaledHit = localHit / max(scale, vec3(1e-8));

    // Return squared Mahalanobis distance
    return dot(scaledHit, scaledHit);
}


float computeGrayDist(
    vec3 hitPoint,
    vec3 particlePosition,
    vec4 quaternion,
    vec3 scale)
{
    mat3 rotationMatrix = quaternionToMatrix(quaternion);
    return computeGrayDist(hitPoint, particlePosition, rotationMatrix, scale);
}


// --------------------------------------------------- //
//  Alpha and Weight Computation
// --------------------------------------------------- //

float computeAlpha(float response, float opacity)
{
    // Clamp to 0.99 to prevent log(0) in transmittance calculation
    return min(0.99, response * opacity);
}


float computeWeight(float alpha, float transmittance)
{
    return alpha * transmittance;
}


void updateTransmittance(inout float transmittance, float alpha)
{
    transmittance *= (1.0 - alpha);
}


// --------------------------------------------------- //
//  Complete Gaussian Evaluation
// --------------------------------------------------- //

float evaluateGaussianResponse(
    vec3 hitPoint,
    vec3 particlePosition,
    mat3 rotationMatrix,
    vec3 scale,
    int kernelDegree)
{
    float grayDist = computeGrayDist(hitPoint, particlePosition, rotationMatrix, scale);
    return particleResponse(grayDist, kernelDegree);
}


float evaluateGaussianResponse(
    vec3 hitPoint,
    vec3 particlePosition,
    vec4 quaternion,
    vec3 scale,
    int kernelDegree)
{
    mat3 rotationMatrix = quaternionToMatrix(quaternion);
    return evaluateGaussianResponse(
        hitPoint,
        particlePosition,
        rotationMatrix,
        scale,
        kernelDegree);
}


#endif // VK3DGRT_EVAL_GLSL