#version 460

layout(location=0) out vec2 outTexcoord;

void main()
{
	vec2 positions[4] = {
		vec2(-1.0, 1.0),
		vec2(-1.0,-1.0),
		vec2( 1.0, 1.0),
		vec2( 1.0,-1.0),
	};
	vec2 texcoords[4] = {
		vec2(0.0, 0.0),
		vec2(0.0, 1.0),
		vec2(1.0, 0.0),
		vec2(1.0, 1.0),
	};

	gl_Position = vec4(positions[gl_VertexIndex], 0, 1);
	outTexcoord = texcoords[gl_VertexIndex];
}
