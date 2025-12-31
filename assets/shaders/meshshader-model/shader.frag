#version 460
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define M_PI     (3.1415926535897932384626433832795)
#define M_INV_PI (1.0 / M_PI)

layout(location=0) in VertexInput {
  vec3 normal;
  vec2 uv0;
  vec4 color;
  vec3 tangent;
  vec3 binormal;
  vec3 worldPos;
  flat uint32_t materialIndex;
} vertexInput;

layout(location=0) out vec4 outColor;

layout(set=0,binding=0) uniform SceneParameters
{
  mat4 mtxView;
  mat4 mtxProj;
  vec4 lightDir;
  vec3 eyePosition;
  float exposure;
} sceneParams;

struct Material
{
  vec4 baseColor;

  float metallic;
  float roughness;
  uint  alphaMode;
  uint  _padd0;

  uint  baseColorTexIndex;
  uint  metallicRoughnessTexIndex;
  uint  normalTexIndex;
  float alphaCutoff;
};

layout(set=1,binding=0) readonly buffer MaterialBuffer { Material materials[];};
layout(set=1,binding=1) uniform sampler2D textureList[];


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

vec4 GetBaseColor(Material material)
{
  vec4 baseColor = material.baseColor;
  vec4 fetchedBaseColor = vec4(1.0);
  if(material.baseColorTexIndex != 0xFFFFFFFF)
  {
    fetchedBaseColor = texture(textureList[nonuniformEXT(material.baseColorTexIndex)], vertexInput.uv0);
    fetchedBaseColor = ConvertSRGBtoLINEAR(fetchedBaseColor);
  }
 
  baseColor *= fetchedBaseColor;
  return baseColor;
}

vec3 GetNormalVector(Material material)
{
  vec3 normal = vertexInput.normal;
  if(material.normalTexIndex != 0xFFFFFFFF)
  {
    vec4 normalMap = texture(textureList[nonuniformEXT(material.normalTexIndex)], vertexInput.uv0);
    mat3 mtxTBN = mat3(vertexInput.tangent, vertexInput.binormal, normal);
    normalMap = normalMap * 2.0 - 1.0;
    return normalize(mtxTBN * normalize(normalMap.xyz)); 
  }
  return normal;
}

vec2 GetMetallicRoughness(Material material)
{
  float metallic = material.metallic;
  float roughness = material.roughness;
  if(material.metallicRoughnessTexIndex != 0xFFFFFFFF)
  {
    vec4 metallicRoughness = texture(textureList[nonuniformEXT(material.metallicRoughnessTexIndex)], vertexInput.uv0);
    metallic *= metallicRoughness.b;
    roughness *= metallicRoughness.g;
  }
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
    float attenuationL = 2.0 * dotNL / (dotNL + sqrt(r * r + (1.0 - r * r) * (dotNL * dotNL)));
    float attenuationV = 2.0 * dotNV / (dotNV + sqrt(r * r + (1.0 - r * r) * (dotNV * dotNV)));
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
  vec3 toLightDir = normalize(sceneParams.lightDir.xyz);

  Material material = materials[nonuniformEXT(vertexInput.materialIndex)];
  vec4 baseColor = GetBaseColor(material);

  vec2 metallicRoughness = GetMetallicRoughness(material);
  float metallic = metallicRoughness.x;
  float roughness = metallicRoughness.y;

  vec3 normal = GetNormalVector(material);
  vec3 toView = normalize(sceneParams.eyePosition.xyz - vertexInput.worldPos);
  vec3 H = normalize(toView + toLightDir);  // half-vector

  float dotNL = clamp(dot(toLightDir, normal), 0, 1);
  float dotNV = clamp(dot(normal, toView), 0, 1);
  float dotNH = clamp(dot(normal, H), 0, 1);
  float dotVH = clamp(dot(toView, H), 0, 1);
  const vec3 F0 = vec3(0.04);
  const vec3 lightColor = vec3(1.0);
  const float alphaRoughness = roughness * roughness;
  
  vec3 diffuse = baseColor.rgb * (vec3(1.0) - F0);
  diffuse *= 1.0 - metallic;
  vec3 specular = mix(F0, baseColor.xyz, metallic);

  float D = microfacetDistribution(alphaRoughness, dotNH);
  vec3 F = specularReflection(specular, dotVH);
  float G = geometricOcclusion(alphaRoughness, dotNL, dotNV);

  vec3 diffuseContrib = (1.0 - F) * diffuse * M_INV_PI;
  vec3 specularContrib = D * F * G / max(4.0 * dotNL * dotNV, 0.0001);

  vec3 color = dotNL * lightColor * (diffuseContrib + specularContrib);
  outColor = vec4(color, baseColor.w);


  if(material.alphaMode == 1 && baseColor.a < material.alphaCutoff)
  {
    // MASK モード.
    discard;
  }

  // 露出とトーンマッピングの処理.
  outColor.xyz *= sceneParams.exposure;
  outColor.xyz = PBRNeutralToneMapping(outColor.xyz);

  // 現在のカラーバッファへsRGBで記録する
  outColor = ConvertLINEARtoSRGB(outColor);

  #if 0
  outColor = vertexInput.color;
  #endif
}
