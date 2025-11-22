#version 450

layout(location=0) in vec3 inNormal;
layout(location=1) in vec3 inColor;
layout(location=2) in vec3 inWorldPosition;

layout(location=0) out vec4 outColor;

layout(set=0,binding=0)
uniform SceneConstants
{
  mat4 matWorld;
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
  vec4 eyePosition;
};

void main()
{
  vec3 toLightDir = normalize(lightDir.xyz);
  vec3 N = normalize(inNormal);
  vec3 lightColor = vec3(1.0);
  vec3 light = vec3(0);

  // diffuse
  const vec3 Kd = inColor;      // 拡散反射係数
  float dotNL = dot(N, toLightDir);
  light += lightColor * Kd * max(dotNL, 0);

  // specular
  if(dotNL > 0)
  {
    const vec3 Ks = vec3(1.0); // 鏡面反射係数
    vec3 toEyeDir = normalize(eyePosition.xyz - inWorldPosition.xyz);
    vec3 R = normalize(reflect(-toLightDir, N));
    const float shininess = 20;
    light += lightColor * Ks * pow(max(dot(toEyeDir, R), 0), shininess);
  }

  // ambient
  const vec3 Ka = inColor; // 環境光反射係数
  vec3 ambientLight = vec3(0.1);
  light += ambientLight * Ka;

  outColor = vec4(light, 1);
}
