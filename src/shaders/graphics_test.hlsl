#include "resources.hlsl"

//Generate black triangle

float4 mainVS(uint vid : SV_VertexID) : SV_POSITION {
	return float4((float)(vid & 1) * 2 - 1, (float)(vid >> 1) * 2 - 1, 0, 1);
}

float4 mainPS() : SV_TARGET {
	return float4(0.xxx, 1);
}
