#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location=0) out vec2 outUV;

layout(set=0,binding=0)
uniform SceneConstants
{
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
  vec4 eyePos;
  vec4 ambientColor;
};
layout(push_constant)
uniform PushConstants
{
  mat4 mtxWorld;
};

void main() {
  vec4 worldPosition = mtxWorld * vec4(inPos, 1.0);
  gl_Position = matProj * matView * worldPosition;
  outUV = inUV;
}
