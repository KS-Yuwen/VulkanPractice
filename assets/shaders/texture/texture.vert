#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec2 inUV;

layout(location=0) out vec2 outUV;

layout(set=0, binding=0)
uniform SceneConstants
{
    mat4 matWorld;
    mat4 matView;
    mat4 matProj;
};

void main()
{
    vec4 worldPosition = matWorld * vec4(inPos, 1.0);
    gl_Position = matProj * matView * worldPosition;
    outUV = inUV;
}