#version 460
#extension GL_EXT_ray_tracing        : require
#extension GL_EXT_buffer_reference   : require
#extension GL_EXT_scalar_block_layout: require

#include "utils/common.glsl"

layout(set = 0, binding = 4, scalar) readonly buffer MeshVertexBuf
{
    vec3 data[];
} meshVertices;

layout(set = 0, binding = 5, scalar) readonly buffer MeshNormalBuf
{
    vec3 data[];
} meshNormals;

layout(set = 0, binding = 6, scalar) readonly buffer MeshIndexBuf
{
    uvec3 data[];
} meshIndices;

layout(set = 0, binding = 7, std430) readonly buffer MeshMaterialBuf
{
    MeshMaterialGPU data[];
} meshMaterials;

layout(location = 1) rayPayloadInEXT MeshHitPayload meshPayload;

hitAttributeEXT vec2 hitBarycentrics;


void main()
{
    uint meshIndex = gl_InstanceCustomIndexEXT;

    MeshMaterialGPU material = meshMaterials.data[meshIndex];

    // gl_PrimitiveID is local to this BLAS geometry;
    // offset by the mesh's triangle start in the combined index buffer
    uvec3 idx = meshIndices.data[gl_PrimitiveID + material.indexOffset];

    // Barycentric interpolation of per-vertex normals
    vec3 barycentrics = vec3(
        1.0 - hitBarycentrics.x - hitBarycentrics.y,
        hitBarycentrics.x,
        hitBarycentrics.y
    );

    vec3 n0 = meshNormals.data[idx.x];
    vec3 n1 = meshNormals.data[idx.y];
    vec3 n2 = meshNormals.data[idx.z];

    vec3 normal = normalize(
        barycentrics.x * n0 +
        barycentrics.y * n1 +
        barycentrics.z * n2
    );

    // Transform normal to world space (geometry normal, unflipped)
    normal = normalize(gl_ObjectToWorldEXT * vec4(normal, 0.0));

    // For non-refractive materials, flip normal to face the ray.
    // For refractive materials, preserve geometry normal so raygen can
    // determine entry/exit direction via dot(I, N) sign.
    if (material.materialType != MATERIAL_REFRACTIVE)
    {
        if (dot(normal, gl_WorldRayDirectionEXT) > 0.0)
        {
            normal = -normal;
        }
    }

    meshPayload.normal       = normal;
    meshPayload.hitDist      = gl_HitTEXT;
    meshPayload.color        = material.color;
    meshPayload.materialType = material.materialType;
    meshPayload.reflectivity = material.reflectivity;
    meshPayload.ior          = material.ior;
}