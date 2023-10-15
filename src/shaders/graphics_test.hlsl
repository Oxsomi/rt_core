#include "resources.hlsl"

//Generate black triangle

float4 mainVS(float2 pos : TEXCOORD0) : SV_POSITION {
	return float4(pos, 0, 1);
}

float4 mainPS() : SV_TARGET {
	return float4(0.xxx, 1);
}
