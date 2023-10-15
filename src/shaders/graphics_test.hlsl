#include "resources.hlsl"

//Generate black triangle

struct VSOutput {
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

VSOutput mainVS(float2 pos : TEXCOORD0, float2 uv : TEXCOORD1) {
	VSOutput output = (VSOutput) 0;
	output.pos = float4(pos, 0, 1);
	output.uv = uv;
	return output;
}

float4 mainPS(float2 uv : TEXCOORD0): SV_TARGET {
	return float4(0, uv, 1);
}
