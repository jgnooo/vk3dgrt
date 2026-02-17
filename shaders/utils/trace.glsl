#ifndef VK3DGRT_TRACE_GLSL
#define VK3DGRT_TRACE_GLSL

#include "common.glsl"


// Fetch a single vec3 SH coefficient from packed fp16 buffer.
// Each coefficient = 3 fp16 (RGB) packed into uint32 pairs.
// shIdx must be a compile-time literal for optimal codegen
// (compiler resolves fi/2, fi%2 at compile time → no branch).
vec3 fetchSHCoeff(uint shBase, int shIdx)
{
    int fi = shIdx * 3;
    vec2 p0 = unpackHalf2x16(shBuffer.data[shBase + fi / 2]);
    vec2 p1 = unpackHalf2x16(shBuffer.data[shBase + (fi + 1) / 2]);
    vec2 p2 = unpackHalf2x16(shBuffer.data[shBase + (fi + 2) / 2]);
    return vec3(
        (fi % 2 == 0)       ? p0.x : p0.y,
        ((fi + 1) % 2 == 0) ? p1.x : p1.y,
        ((fi + 2) % 2 == 0) ? p2.x : p2.y
    );
}


// Streaming SH evaluation: no local array, reads each coefficient on demand.
// Register usage: vec3 x 1 (reused) instead of vec3 x 16.
// Only reads coefficients needed for the given maxDegree.
vec3 evaluateParticleSH(uint particleId, vec3 viewDir, int maxDegree)
{
    uint shBase = particleId * 24;

    vec3  d = normalize(viewDir);
    float x = d.x;
    float y = d.y;
    float z = d.z;

    // ── Degree 0 (1 coefficient) ──
    vec3 result = vec3(0.5) + SH_C0 * fetchSHCoeff(shBase, 0);

    if (maxDegree < 1)
    {
        return max(result, vec3(0.0));
    }

    // ── Degree 1 (3 coefficients) ──
    result += SH_C1 * (-y) * fetchSHCoeff(shBase, 1);
    result += SH_C1 *   z  * fetchSHCoeff(shBase, 2);
    result += SH_C1 * (-x) * fetchSHCoeff(shBase, 3);

    if (maxDegree < 2)
    {
        return max(result, vec3(0.0));
    }

    // ── Degree 2 (5 coefficients) ──
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;

    result += SH_C2[0] * xy * fetchSHCoeff(shBase, 4);
    result += SH_C2[1] * yz * fetchSHCoeff(shBase, 5);
    result += SH_C2[2] * (2.0 * zz - xx - yy) * fetchSHCoeff(shBase, 6);
    result += SH_C2[3] * xz * fetchSHCoeff(shBase, 7);
    result += SH_C2[4] * (xx - yy) * fetchSHCoeff(shBase, 8);

    if (maxDegree < 3)
    {
        return max(result, vec3(0.0));
    }

    // ── Degree 3 (7 coefficients) ──
    result += SH_C3[0] * y * (3.0 * xx - yy) * fetchSHCoeff(shBase, 9);
    result += SH_C3[1] * xy * z * fetchSHCoeff(shBase, 10);
    result += SH_C3[2] * y * (4.0 * zz - xx - yy) * fetchSHCoeff(shBase, 11);
    result += SH_C3[3] * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * fetchSHCoeff(shBase, 12);
    result += SH_C3[4] * x * (4.0 * zz - xx - yy) * fetchSHCoeff(shBase, 13);
    result += SH_C3[5] * z * (xx - yy) * fetchSHCoeff(shBase, 14);
    result += SH_C3[6] * x * (xx - 3.0 * yy)* fetchSHCoeff(shBase, 15);

    return max(result, vec3(0.0));
}


void processParticleHit(uint  particleId,
                        float hitDist,
                        vec3  rOrigin,
                        vec3  rDirection,
                        inout vec3  radiance,
                        inout float transmittance,
                        inout float depth)
{
    if (sceneBounds.renderMode == RENDER_MODE_POINT)
    {
        // Point visualization: simple opaque dots
        vec3 pPosition = positionBuffer.data[particleId];
        vec4 pColor    = colorBuffer.data[particleId];

        vec3 toCenter  = pPosition - rOrigin;
        float t        = dot(toCenter, rDirection);
        vec3 closestPt = rOrigin + t * rDirection;
        float dist     = length(pPosition - closestPt);

        if (dist <= 0.003)
        {
            radiance += pColor.rgb * transmittance;
            depth    += t * transmittance;
            transmittance = 0.0;
        }
    }
    else if (sceneBounds.renderMode == RENDER_MODE_SPLAT)
    {
        // Splat visualization: 2D circular falloff
        vec3  pPosition   = positionBuffer.data[particleId];
        vec4  pColor      = colorBuffer.data[particleId];
        vec4  pQuaternion = quaternionBuffer.data[particleId];
        vec3  pScale      = scaleBuffer.data[particleId];

        float pOpacity = pColor.a;

        mat3 R        = quaternionToMatrix(pQuaternion);
        mat3 RT       = transpose(R);
        vec3 invScale = 1.0 / max(pScale, vec3(1e-8));

        vec3 offset = rOrigin - pPosition;
        vec3 o_g    = invScale * (RT * offset);
        vec3 d_g    = invScale * (RT * rDirection);

        float d_g_dot = dot(d_g, d_g);
        float d_val   = -dot(o_g, d_g) / max(1e-6, d_g_dot);

        vec3 pos  = rOrigin + d_val * rDirection;
        vec3 p_g  = invScale * (RT * (pPosition - pos));
        float dist2D = length(p_g.xy);

        float response = exp(-0.5 * dist2D * dist2D);
        if (response >= MIN_PARTICLE_KERNEL_DENSITY)
        {
            float alpha = min(response * pOpacity, 0.99);
            if (alpha >= MIN_PARTICLE_ALPHA)
            {
                float weight = alpha * transmittance;
                radiance += pColor.rgb * weight;
                depth    += max(d_val, 0.0) * weight;
                transmittance *= (1.0 - alpha);
            }
        }
    }
    else if (sceneBounds.renderMode == RENDER_MODE_GS)
    {
        // 1. Load particle color + opacity (early cull check)
        vec4  pColor   = colorBuffer.data[particleId];
        float pOpacity = pColor.a;

        // 2. Density pre-cull (nvpro: particleDensity > alphaCullThreshold)
        //    Skip particles with negligible opacity before expensive math
        if (pOpacity > MIN_PARTICLE_ALPHA)
        {
            // 3. Load remaining particle data
            vec3  pPosition   = positionBuffer.data[particleId];
            vec4  pQuaternion = quaternionBuffer.data[particleId];
            vec3  pScale      = scaleBuffer.data[particleId];

            // 4. Transform ray to particle's local coordinate system
            mat3 R        = quaternionToMatrix(pQuaternion);
            mat3 RT       = transpose(R);
            vec3 invScale = 1.0 / max(pScale, vec3(1e-8));

            vec3 offset = rOrigin - pPosition;
            vec3 o_g    = invScale * (RT * offset);
            vec3 d_g    = invScale * (RT * rDirection);

            // 5. Compute closest point parameter (for depth) and grayDist
            float d_g_dot = dot(d_g, d_g);
            float d_val   = -dot(o_g, d_g) / max(1e-6, d_g_dot);

            // 6. grayDist via cross-product (nvpro optimization)
            vec3 d_norm    = d_g * inversesqrt(max(1e-6, d_g_dot));
            vec3 gcrod     = cross(d_norm, o_g);
            float grayDist = dot(gcrod, gcrod);

            // 7. Compute particle response using runtime kernel degree
            float response = particleResponse(grayDist, int(sceneBounds.kernelDegree));

            // 8. Early exit for negligible response
            if (response >= MIN_PARTICLE_KERNEL_DENSITY)
            {
                // 9. Compute alpha and check threshold
                float alpha = min(0.99, response * pOpacity);
                if (alpha >= MIN_PARTICLE_ALPHA)
                {
                    // 10. Evaluate color (DC or SH, runtime degree via UBO)
                    vec3 finalColor = pColor.rgb;

                    if (sceneBounds.hasSH > 0 && sceneBounds.shDegree > 0)
                    {
                        // View-dependent color via Spherical Harmonics
                        // Use direction to particle center (nvpro approach, more physically correct)
                        vec3 viewDir = normalize(pPosition - rOrigin);
                        finalColor = evaluateParticleSH(particleId, viewDir, int(sceneBounds.shDegree));
                    }

                    // 11. Accumulate using Over operator (front-to-back)
                    float weight = alpha * transmittance;
                    radiance += finalColor * weight;
                    depth    += max(d_val, 0.0) * weight;
                    transmittance *= (1.0 - alpha);
                }
            }
        }
    }
}


void traceGaussians(vec3  rOrigin,
                    vec3  rDirection,
                    float tMinVal,
                    float tMaxVal,
                    int   maxIter,
                    inout vec3  radiance,
                    inout float transmittance,
                    inout float depth)
{
    float rayLastHitDist = max(0.0, tMinVal - T_EPSILON);

    uint rayFlags = gl_RayFlagsSkipClosestHitShaderEXT
                  | gl_RayFlagsCullBackFacingTrianglesEXT;

    int iteration = 0;
    while (transmittance > TRANSMITTANCE_THRESHOLD &&
           rayLastHitDist <= tMaxVal &&
           iteration < maxIter)
    {
        // Initialize payload with invalid hits
        for (int i = 0; i < MAX_HITS_PER_TRACE; i++)
        {
            payload.id[i]   = INVALID_PARTICLE_ID;
            payload.dist[i] = INFINITE_DISTANCE;
        }

        // Trace Gaussian TLAS (unchanged from existing)
        traceRayEXT(
            tlas,
            rayFlags,
            0xFF,
            0, 0, 0,
            rOrigin,
            rayLastHitDist + T_EPSILON,
            rDirection,
            tMaxVal,                   // Render particles only up to mesh hit
            0
        );

        if (payload.id[0] == INVALID_PARTICLE_ID)
        {
            break;
        }

        // Process all valid hits in sorted order (front-to-back)
        for (int i = 0; i < MAX_HITS_PER_TRACE; i++)
        {
            uint particleId = payload.id[i];
            if (particleId == INVALID_PARTICLE_ID)
            {
                break;
            }

            float hitDist = payload.dist[i];

            // Track farthest processed hit (for tMin advancement)
            rayLastHitDist = max(rayLastHitDist, hitDist);

            processParticleHit(
                particleId, 
                hitDist, 
                rOrigin, 
                rDirection, 
                radiance, 
                transmittance, 
                depth
            );

            if (transmittance <= TRANSMITTANCE_THRESHOLD)
            {
                break;
            }
        }

        iteration++;
    }
}

#endif // VK3DGRT_TRACE_GLSL