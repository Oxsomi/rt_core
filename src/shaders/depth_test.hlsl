#include "resource_bindings.hlsl"

//Generate black triangle

struct VSOutput {
	F32x4 pos : SV_POSITION;
	F32x2 uv : _bind(0);
};

static const U32 quadIndices[] = {
	00132,		//Bottom
	06754,		//Top
	00451,		//Back
	02376,		//Front
	03157,		//Right
	00264		//Left
};

VSOutput mainVS(U32 id : SV_VertexID) {

	//Generate quad

	U32 side = id / 6;
	U32 tri = id % 6;
	U32 triVertId = tri % 3;
	U32 triId = tri / 3;

	U32 indexId = (triVertId + (triId << 1)) & 3;			//0, 1, 2,  2, 3, 0
	U32 index = (quadIndices[side] >> (indexId * 3)) & 07;

	//Generate positions of quads

	F32x3 mpos = ((index.xxx >> uint3(0, 2, 1)) & 1) - 0.5f;
	F32x2 uv = float2(indexId & 1, indexId >> 1);

	F32x4x4 m = F32x4x4_transform(0.xxx, 0.xxx, 1.xxx);		//pos, rot, scale
	F32x3 wpos = mul(F32x4(mpos, 1), m).xyz;

	U32 viewProjMatBuf = getAppData1u(EResourceBinding_ViewProjMatrices);
	ViewProjMatrices viewProjMat = getAtUniform<ViewProjMatrices>(viewProjMatBuf, 0);

	F32x4 cpos = mul(F32x4(wpos, 1), viewProjMat.viewProj);

	VSOutput output = (VSOutput) 0;
	output.pos = cpos;
	output.uv = uv;
	return output;
}
