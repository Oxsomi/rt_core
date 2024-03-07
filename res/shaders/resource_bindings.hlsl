#include "resources.hlsl"

enum EResourceBinding {
	EResourceBinding_ConstantColorBuffer,
	EResourceBinding_ConstantColorBufferRW,
	EResourceBinding_IndirectDrawRW,
	EResourceBinding_IndirectDispatchRW,
	EResourceBinding_ViewProjMatricesRW,
	EResourceBinding_ViewProjMatrices,
	EResourceBinding_Crabbage2049x,
	EResourceBinding_CrabbageCompressed,
	EResourceBinding_Sampler,
	EResourceBinding_TLAS,
	EResourceBinding_RenderTargetRW
};

struct ViewProjMatrices {
	F32x4x4 view, proj, viewProj;
	F32x4x4 viewInv, projInv, viewProjInv;
};
