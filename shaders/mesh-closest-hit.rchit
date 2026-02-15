#version 460
#extension GL_EXT_ray_tracing        : require
#extension GL_EXT_buffer_reference   : require
#extension GL_EXT_scalar_block_layout: require

#include "utils/common.glsl"

layout(set = 0, binding = 4, scalar) readonly buffer MeshVertexBuf
{
    vec3 data[];
} meshVertices;

layout(set = 0, binding = 5, scalar) readonly buffer MeshIndexBuf
{
    uvec3 data[];
} meshIndices;

layout(set = 0, binding = 6, std430) readonly buffer MeshMaterialBuf
{
    MeshMaterialGPU data[];
} meshMaterials;

layout(location = 1) rayPayloadInEXT MeshHitPayload meshPayload;


void main()
{
    uint meshIndex = gl_InstanceCustomIndexEXT;

    uvec3 idx = meshIndices.data[gl_PrimitiveID];
    vec3 v0 = meshVertices.data[idx.x];
    vec3 v1 = meshVertices.data[idx.y];
    vec3 v2 = meshVertices.data[idx.z];

    // Normal calculation
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;
    vec3 normal = normalize(cross(edge1, edge2));

    if (dot(normal, gl_WorldRayDirectionEXT) > 0.0)
    {
        normal = -normal; // Flip normal to face the ray
    }

    MeshMaterialGPU material = meshMaterials.data[meshIndex];

    meshPayload.normal       = normal;
    meshPayload.hitDist      = gl_HitTEXT;
    meshPayload.color        = material.color;
    meshPayload.materialType = material.materialType;
    meshPayload.reflectivity = material.reflectivity;
}