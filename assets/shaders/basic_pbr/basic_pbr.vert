#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inTexcoord;
layout(location=3) in vec3 inTangent;
layout(location=4) in vec3 inBinormal;


layout(location=0) out vec2 outTexcoord;
layout(location=1) out vec3 outNormal;
layout(location=2) out vec3 outWorldPos;
layout(location=3) out vec3 outTangent;
layout(location=4) out vec3 outBinormal;

layout(set=0,binding=0)
uniform SceneConstants
{
  mat4 mtxView;
  mat4 mtxProj;
  vec4 lightDir;
  vec3 eyePos;
  float exposure;
};

layout(push_constant)
uniform PushConstants
{
  mat4 mtxWorld;
};

void main()
{
  vec4 worldPosition = mtxWorld * vec4(inPos, 1.0);
  gl_Position = mtxProj * mtxView * worldPosition;
  outWorldPos = worldPosition.xyz;
  outTexcoord = inTexcoord;

  mat3 mtxWorld33 = mat3(mtxWorld);
  outNormal = normalize(mtxWorld33 * inNormal);
  outTangent = normalize(mtxWorld33 * inTangent);
  outBinormal = normalize(mtxWorld33 * inBinormal);
}
