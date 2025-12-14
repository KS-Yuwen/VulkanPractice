#version 450

layout(quads,equal_spacing, ccw) in;

layout(location=0) out vec3 outWorldPos;
layout(location=1) out vec3 outWorldNormal;

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
layout(push_constant)
uniform PushConstants
{
  mat4 mtxWorld;
};


vec3 GetWavePos(vec3 position)
{
  float time = frameTime * 0.01;
  float atten = 1.0 - length(position) / 10.0;
  float waveHeight = sin(position.x + time) * cos(position.z + time) * atten;
  return vec3(position.x, position.y+waveHeight, position.z);
}
void main()
{
  vec3 domain = gl_TessCoord;
  vec4 p0 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, domain.x);
  vec4 p1 = mix(gl_in[2].gl_Position, gl_in[3].gl_Position, domain.x);
  vec4 basePosition = mix(p0, p1, domain.y);

  // 位置の変位.
  vec3 position = GetWavePos(basePosition.xyz);
  // 法線の計算.
  const float eps = 0.001;
  vec3 e1 = normalize(GetWavePos(basePosition.xyz+vec3(eps, 0, 0)) - position.xyz);
  vec3 e2 = normalize(GetWavePos(basePosition.xyz+vec3(0, 0, eps)) - position.xyz);
  vec3 normal = normalize(cross(e2, e1));

  vec4 worldPosition = mtxWorld * vec4(position.xyz, 1.0);
  gl_Position = matProj * matView * worldPosition;

  outWorldPos.xyz = worldPosition.xyz;
  outWorldNormal.xyz =  mat3(mtxWorld) * normal;
}
