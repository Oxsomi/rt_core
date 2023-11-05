#include "resources.hlsl"

//Generate black triangle

struct VSOutput {
	F32x4 pos : SV_POSITION;
	F32x2 uv : _bind(0);
};

VSOutput mainVS(F32x2 pos : _bind(0), F32x2 uv : _bind(1)) {
	VSOutput output = (VSOutput) 0;
	output.pos = F32x4(pos, 0, 1);
	output.uv = uv;
	return output;
}

F32x4 mainPS(F32x2 uv : _bind(0)): SV_TARGET {

	U32 resourceId = getAppData1u(_swapchainCount);
	F32x3 col = getAtUniform<F32x3>(resourceId, 0);		//Randomly output from compute shader (0 at init)

	return F32x4(F32x3(0, uv) + col, 1);
}
