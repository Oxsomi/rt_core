#include "resource_bindings.hlsl"

[numthreads(1, 1, 1)]
void main(U32 i : SV_DispatchThreadID) {
	U32 resourceId = getAppData1u(EResourceBinding_ConstantColorBufferRW);
	F32x3 col = sin(_time.xxx * F32x3(0.5, 0.25, 0.125)) * 0.5 + 0.5;
	setAtUniform(resourceId, 0, col);
}
