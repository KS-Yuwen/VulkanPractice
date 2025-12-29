#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#include "hitpayload.glsli"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main()
{
    // ベクトルのY成分から背景となる色を計算する.
    vec3 dir = normalize(gl_WorldRayDirectionEXT);
    float t = 0.5 * (dir.y + 1.0);
    // 背景(空)の計算.
    //vec3 sky = vec3(0.75, 0.9, 1.0);
    vec3 sky = vec3(0.1, 0.45, 0.9);
    vec3 ground= vec3(0.0);
    payload.color = mix(ground, sky, t);

    payload.materialKind = -1;
    payload.roughness = 1;
    payload.rayTerminate = true;
}
