#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable
#include "hitpayload.glsli"

layout(location = 1) rayPayloadInEXT ShadowHitPayload payload;

void main()
{
    payload.isOccluded = false;
}
