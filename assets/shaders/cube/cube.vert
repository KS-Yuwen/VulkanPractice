#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location=2) in vec3 inColor;

layout(location=0) out vec3 outNormal;
layout(location=1) out vec3 outColor;
layout(location=2) out vec3 outWorldPosition;

layout(set=0,binding=0)
uniform SceneConstants
{
  mat4 matWorld;
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
  vec4 eyePosition;
};

void main() {
  vec4 worldPosition = matWorld * vec4(inPos, 1.0);
  gl_Position = matProj * matView * worldPosition;
  outNormal = mat3(matWorld) * inNormal;
  outColor = inColor;
  outWorldPosition = worldPosition.xyz;
}
