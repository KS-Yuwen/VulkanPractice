#version 450
#define M_PI     (3.1415926535897932384626433832795)
#define M_INV_PI (1.0 / M_PI)

layout(location=0) in vec2 inTexcoord;
layout(location=1) in vec3 inNormal; // in worldSpace
layout(location=2) in vec3 inWorldPos;
layout(location=3) in vec3 inTangent; // in worldSpace
layout(location=4) in vec3 inBinormal;// in worldSpace

layout(location=0) out vec4 outColor;

layout(set=0,binding=0)
uniform SceneConstants
{
  mat4 matView;
  mat4 matProj;
  vec4 lightDir;
  vec3 eyePos;
  float exposure;
};

layout(set=1,binding=0)
uniform MeshMaterialParameters
{
  vec4 baseColorFactor;
  float metallicFactor;
  float roughnessFactor;
  float alphaCutoff;
  uint  alphaMode;

  uint  hasNormalMap; // 法線マップ有効時に1となる
  uint  padd0;
  uint  padd1;
  uint  padd2;
};
layout(set=1,binding=1)
uniform sampler2D texBaseColor;
layout(set=1,binding=2)
uniform sampler2D texMetallicRoughness;
layout(set=1,binding=3)
uniform sampler2D texNormalMap;

vec4 ConvertSRGBtoLINEAR(vec4 inSRGB)
{
  vec3 c = pow(inSRGB.xyz,vec3(2.2));
  return vec4(c, inSRGB.w);
}
vec4 ConvertLINEARtoSRGB(vec4 inLinear)
{
  vec3 c = pow(inLinear.xyz, vec3(1.0/2.2));
  return vec4(c, inLinear.a);
}

vec4 GetBaseColor()
{
  vec4 baseColor = baseColorFactor;
  vec4 fetchedBaseColor = texture(texBaseColor, inTexcoord);
  baseColor *= ConvertSRGBtoLINEAR(fetchedBaseColor);
  return baseColor;
}

vec3 GetNormalVector()
{
  if(hasNormalMap > 0)
  {
    vec4 normalMap = texture(texNormalMap, inTexcoord);
    mat3 mtxTBN = mat3(inTangent, inBinormal, inNormal);
    normalMap = normalMap * 2.0 - 1.0;
    return normalize(mtxTBN * normalize(normalMap.xyz)); 
  }
  return normalize(inNormal);
}

vec2 GetMetallicRoughness()
{
  vec4 metallicRoughness = texture(texMetallicRoughness, inTexcoord);
  float metallic = metallicFactor * metallicRoughness.b;
  float roughness = roughnessFactor * metallicRoughness.g;
  return vec2(metallic, roughness);
}

vec3 specularReflection(vec3 reflectance0, float dotVH)
{
  float x = clamp(1.0 - dotVH, 0.0, 1.0);
  return reflectance0 + (vec3(1.0) - reflectance0) * pow(x, 5.0);
}
float microfacetDistribution(float alphaRoughness, float dotNH)
{
  float roughnessSq = alphaRoughness * alphaRoughness;
  float f = (dotNH * roughnessSq - dotNH) * dotNH + 1.0;
  return roughnessSq / (M_PI * f * f);
}

float geometricOcclusion(float alphaRoughness, float dotNL, float dotNV)
{
    float r = alphaRoughness;
    // 分母がゼロになるのを防ぐために、微小な値を加える
    float epsilon = 1e-5;
    float attenuationL = 2.0 * dotNL / (dotNL + sqrt(r * r + (1.0 - r * r) * (dotNL * dotNL)) +epsilon);
    float attenuationV = 2.0 * dotNV / (dotNV + sqrt(r * r + (1.0 - r * r) * (dotNV * dotNV)) +epsilon);
    return attenuationL * attenuationV;
}

vec3 PBRNeutralToneMapping( vec3 color ) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return mix(color, newPeak * vec3(1, 1, 1), g);
}

void main()
{
  vec3 toLightDir = normalize(lightDir.xyz);
  vec4 baseColor = GetBaseColor();

  vec2 metallicRoughness = GetMetallicRoughness();
  float metallic = metallicRoughness.x;
  float roughness = metallicRoughness.y;

  vec3 normal = GetNormalVector();
  vec3 toView = normalize(eyePos.xyz - inWorldPos);
  vec3 H = normalize(toView + toLightDir);  // half-vector

  float dotNL = clamp(dot(toLightDir, normal), 0, 1);
  float dotNV = clamp(dot(normal, toView), 0, 1);
  float dotNH = clamp(dot(normal, H), 0, 1);
  float dotVH = clamp(dot(toView, H), 0, 1);
  const vec3 F0 = vec3(0.04);
  const vec3 lightColor = vec3(1.0);
  const float alphaRoughness = roughness * roughness;
  
  vec3 diffuse = baseColor.rgb * (1.0 - metallic);
  vec3 specular = mix(F0, baseColor.xyz, metallic);

  float D = microfacetDistribution(alphaRoughness, dotNH);
  vec3 F = specularReflection(specular, dotVH);
  float G = geometricOcclusion(alphaRoughness, dotNL, dotNV);

  vec3 diffuseContrib = (1.0 - F) * diffuse * M_INV_PI;
  vec3 specularContrib = D * F * G / max(4.0 * dotNL * dotNV, 0.0001);

  vec3 color = dotNL * lightColor * (diffuseContrib + specularContrib);
  outColor = vec4(color, baseColor.w);

  /* ALPHA_MASK */
  if(alphaMode == 1 && outColor.a < alphaCutoff)
  {
    discard;
  }
  
  // 露出とトーンマッピングの処理.
  outColor.xyz *= exposure;
  outColor.xyz = PBRNeutralToneMapping(outColor.xyz);

  // 現在のカラーバッファへsRGBで記録する
  outColor = ConvertLINEARtoSRGB(outColor);

}
