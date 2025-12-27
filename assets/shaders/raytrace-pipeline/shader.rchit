#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "hitpayload.glsli"

struct MaterialParameters
{
  vec4 baseColor;

  float metallic;
  float roughness;
  uint  materialKind;
  uint  _padd0;

  uint  baseColorTexIndex;
  uint  metallicRoughnessTexIndex;
  uint  normalTexIndex;
  uint  _padd1;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

layout(set=0,binding=2) uniform SceneParameters
{
  mat4 mtxView;
  mat4 mtxProj;
  mat4 mtxViewInv;
  mat4 mtxProjInv;
  vec4 lightDirection;
  vec4 eyePosition;
} sceneParams;

// マテリアル情報の配列(可変長)
layout(set=0,binding=3) readonly buffer MaterialBuffer { MaterialParameters materials[];};
// テクスチャ(可変長配列)
layout(set=0,binding=4) uniform sampler2D textureList[];

hitAttributeEXT vec2 attribs;

layout(buffer_reference, buffer_reference_align=4, scalar) readonly buffer Indices { uvec3 i[]; };
layout(buffer_reference, buffer_reference_align=4, scalar) readonly buffer Vector3 { vec3 v[]; };
layout(buffer_reference, buffer_reference_align=4, scalar) readonly buffer Vector2 { vec2 v[]; };

layout(shaderRecordEXT) buffer shaderRecord
{
  uint64_t indexBuffer;
  uint64_t vbPosition;
  uint64_t vbNormal;
  uint64_t vbTexcoord;
  uint32_t materialIndex;
};


void main() {
  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  Indices indices = Indices(indexBuffer);
  Vector3 bufferPosition = Vector3(vbPosition);
  Vector3 bufferNormal = Vector3(vbNormal);
  Vector2 bufferTexcoord = Vector2(vbTexcoord);

  const uvec3 idx = indices.i[gl_PrimitiveID];
  vec3 position = vec3(0);
  position += bufferPosition.v[idx.x] * barys.x;
  position += bufferPosition.v[idx.y] * barys.y;
  position += bufferPosition.v[idx.z] * barys.z;
  // 法線とUVについても同様の計算.
  vec3 normal = vec3(0);
  normal += bufferNormal.v[idx.x] * barys.x;
  normal += bufferNormal.v[idx.y] * barys.y;
  normal += bufferNormal.v[idx.z] * barys.z;
  vec2 texcoord = vec2(0);
  texcoord += bufferTexcoord.v[idx.x] * barys.x;
  texcoord += bufferTexcoord.v[idx.y] * barys.y;
  texcoord += bufferTexcoord.v[idx.z] * barys.z;

  vec3 worldPosition = gl_ObjectToWorldEXT * vec4(position, 1);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * normal;
  worldNormal = normalize(worldNormal);

  MaterialParameters material = materials[nonuniformEXT(materialIndex)];

  vec3 baseColor = material.baseColor.xyz;
  float alpha = material.baseColor.w;
  if(nonuniformEXT(material.baseColorTexIndex) != 0xFFFFFFFF)
  {
    vec4 texBaseColor = texture(textureList[nonuniformEXT(material.baseColorTexIndex)], texcoord.xy);
    baseColor *= texBaseColor.xyz;
    alpha *= texBaseColor.w;
  }

  // metallicRoughness
  float roughness = material.roughness;
  float metallic = material.metallic;
  if(nonuniformEXT(material.metallicRoughnessTexIndex) != 0xFFFFFFFF)
  {
    vec4 metallicRoughness = texture(textureList[nonuniformEXT(material.metallicRoughnessTexIndex)], texcoord.xy);
    roughness *= metallicRoughness.g;
    metallic *= metallicRoughness.b;
  }

  if (material.materialKind == 1)
  {
    if(metallic < 0.5)
    {
      material.materialKind = 0;
    }
  }

  payload.color = baseColor;
  payload.materialKind = int(material.materialKind);
  payload.worldPosition = worldPosition;
  payload.roughness = roughness;
  payload.worldNormal = worldNormal;

  if (material.materialKind == 0)
  {
    // 反射・屈折のマテリアルでないならここでトレース終了.
    payload.rayTerminate = true;
  }
  else
  {
    payload.rayTerminate = false;
  }
}
