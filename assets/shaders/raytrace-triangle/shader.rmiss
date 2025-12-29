#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#include "hitpayload.glsli"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main()
{
    payload.color = vec3(0.6f, 0.2f, 0.3f);
}
