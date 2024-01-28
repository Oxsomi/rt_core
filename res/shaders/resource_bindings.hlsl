#include "resources.hlsl"

enum EResourceBinding {
	EResourceBinding_ConstantColorBuffer,
	EResourceBinding_ConstantColorBufferRW,
	EResourceBinding_IndirectDrawRW,
	EResourceBinding_IndirectDispatchRW,
	EResourceBinding_ViewProjMatricesRW,
	EResourceBinding_ViewProjMatrices,
	EResourceBinding_Crabbage599x,
	EResourceBinding_Crabbage2049x,
	EResourceBinding_Sampler
};

struct ViewProjMatrices {
	F32x4x4 view, proj, viewProj;
};
