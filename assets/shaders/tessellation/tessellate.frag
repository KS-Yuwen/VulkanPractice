#version 450

layout(location=0) in vec3 inWorldPos;
layout(location=1) in vec3 inWorldNormal;

layout(location=0) out vec4 outColor;

layout(set=0,binding=0)
uniform SceneConstants
{
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
  vec4 eyePos;
  vec2 tessParameters;
  uint frameTime;
  uint padding;
};

void main()
{
  outColor = vec4(0.0);
  
  vec3 toLightDir = normalize(lightDir.xyz);
  float dotNL = clamp(dot(inWorldNormal, toLightDir), 0, 1);
  outColor.xyz = vec3(dotNL);// * vec3(0.1, 0.2, 0.5);
}
