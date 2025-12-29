#version 460

layout(location=0) in vec2 inTexcoord;
layout(location=0) out vec4 outColor;

layout(set=0,binding=0)
uniform sampler2D gTex;

void main()
{
  outColor = texture(gTex, inTexcoord);
}
