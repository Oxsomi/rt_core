#include "resource_bindings.hlsl"

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

F32x4 mainPS(VSOutput input): SV_TARGET {

	U32 resourceId = getAppData1u(EResourceBinding_ConstantColorBuffer);
	F32x3 col = getAtUniform<F32x3>(resourceId, 0);		//Randomly output from compute shader (0 at init)

	Texture2D tex = texture2DUniform(getAppData1u(EResourceBinding_Crabbage2049x));
	SamplerState sampler = samplerUniform(getAppData1u(EResourceBinding_Sampler));

	F32x3 col2 = tex.Sample(sampler, input.uv).rgb;

	return F32x4(col2 + col, 1);
}
