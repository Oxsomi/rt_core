#include "resources.hlsl"

enum EResourceBinding {
	EResourceBinding_ConstantColorBuffer,
	EResourceBinding_ConstantColorBufferRW,
	EResourceBinding_IndirectDrawRW,
	EResourceBinding_IndirectDispatchRW,
	EResourceBinding_ViewProjMatricesRW,
	EResourceBinding_ViewProjMatrices
};

struct ViewProjMatrices {
	F32x4x4 view, proj, viewProj;
};
