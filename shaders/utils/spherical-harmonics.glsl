// spherical-harmonics.glsl - Spherical Harmonics evaluation for 3DGRT
// Implements degree 3 SH (16 coefficients per channel, 48 total for RGB)
// Based on the official 3DGRT implementation (https://github.com/nv-tlabs/3dgrut)

#ifndef VK3DGRT_SPHERICAL_HARMONICS_GLSL
#define VK3DGRT_SPHERICAL_HARMONICS_GLSL


// --------------------------------------------------- //
//  SH Constants (from official 3DGRT implementation)
// --------------------------------------------------- //

// SH degree 3 = (l+1)^2 = 16 coefficients per channel
#define SH_DEGREE      3
#define SH_COEFFS      16
#define SH_TOTAL_RGB   48

// Degree 0: C0 = 1 / (2 * sqrt(pi))
const float SH_C0 = 0.28209479177387814;

// Degree 1: C1 = sqrt(3) / (2 * sqrt(pi))
const float SH_C1 = 0.4886025119029199;

// Degree 2 coefficients (5 terms)
const float SH_C2[5] = float[5](
    1.0925484305920792,   // SH_C2_0: sqrt(15) / (2 * sqrt(pi))
    -1.0925484305920792,  // SH_C2_1: -sqrt(15) / (2 * sqrt(pi))
    0.31539156525252005,  // SH_C2_2: sqrt(5) / (4 * sqrt(pi))
    -1.0925484305920792,  // SH_C2_3: -sqrt(15) / (2 * sqrt(pi))
    0.5462742152960396    // SH_C2_4: sqrt(15) / (4 * sqrt(pi))
);

// Degree 3 coefficients (7 terms)
const float SH_C3[7] = float[7](
    -0.5900435899266435,  // SH_C3_0: -sqrt(70) / (8 * sqrt(pi))
    2.890611442640554,    // SH_C3_1: sqrt(105) / (2 * sqrt(pi))
    -0.4570457994644658,  // SH_C3_2: -sqrt(42) / (8 * sqrt(pi))
    0.3731763325901154,   // SH_C3_3: sqrt(7) / (4 * sqrt(pi))
    -0.4570457994644658,  // SH_C3_4: -sqrt(42) / (8 * sqrt(pi))
    1.445305721320277,    // SH_C3_5: sqrt(105) / (4 * sqrt(pi))
    -0.5900435899266435   // SH_C3_6: -sqrt(70) / (8 * sqrt(pi))
);

// --------------------------------------------------- //
//  SH Coefficient Fetch Functions
//  These require the SH buffer to be bound before use
// --------------------------------------------------- //

#ifdef SH_BUFFER_BINDING

vec3 fetchSH(uint particleIdx, uint shIndex)
{
    uint baseIdx = particleIdx * SH_TOTAL_RGB + shIndex * 3;
    return vec3(
        shCoeffsBuffer[baseIdx + 0],
        shCoeffsBuffer[baseIdx + 1],
        shCoeffsBuffer[baseIdx + 2]
    );
}
#endif


// --------------------------------------------------- //
//  SH Evaluation Functions (Official 3DGRT Pattern)
// --------------------------------------------------- //

vec3 evaluateSHWithDegree(vec3 direction, vec3 sh[16], int maxDegree)
{
    vec3 d   = normalize(direction);
    float x  = d.x;
    float y  = d.y;
    float z  = d.z;

    // Degree 0 (always included, with 0.5 bias)
    vec3 result = vec3(0.5) + SH_C0 * sh[0];

    if (maxDegree < 1)
    {
        return max(result, vec3(0.0));
    }

    // Degree 1
    result += SH_C1 * (-y * sh[1] + z * sh[2] - x * sh[3]);

    if (maxDegree < 2)
    {
        return max(result, vec3(0.0));
    }

    // Degree 2
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;

    result += SH_C2[0] * xy * sh[4];
    result += SH_C2[1] * yz * sh[5];
    result += SH_C2[2] * (2.0 * zz - xx - yy) * sh[6];
    result += SH_C2[3] * xz * sh[7];
    result += SH_C2[4] * (xx - yy) * sh[8];

    if (maxDegree < 3)
    {
        return max(result, vec3(0.0));
    }

    // Degree 3
    result += SH_C3[0] * y * (3.0 * xx - yy) * sh[9];
    result += SH_C3[1] * xy * z * sh[10];
    result += SH_C3[2] * y * (4.0 * zz - xx - yy) * sh[11];
    result += SH_C3[3] * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * sh[12];
    result += SH_C3[4] * x * (4.0 * zz - xx - yy) * sh[13];
    result += SH_C3[5] * z * (xx - yy) * sh[14];
    result += SH_C3[6] * x * (xx - 3.0 * yy) * sh[15];

    return max(result, vec3(0.0));
}


#endif // VK3DGRT_SPHERICAL_HARMONICS_GLSL
