#version 450

//layout(location=0) out vec2 outUV;
//layout(location=1) out vec3 outNormal;
//layout(location=2) out vec3 outWorldPos;

layout(vertices=4) out;

in gl_PerVertex
{
  vec4 gl_Position;
} gl_in[gl_MaxPatchVertices];


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
  if (gl_InvocationID == 0)
  {
    gl_TessLevelInner[0] = tessParameters.x;
    gl_TessLevelInner[1] = tessParameters.x;   

    gl_TessLevelOuter[0] = tessParameters.y;
    gl_TessLevelOuter[1] = tessParameters.y;
    gl_TessLevelOuter[2] = tessParameters.y;
    gl_TessLevelOuter[3] = tessParameters.y;
  }

  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
