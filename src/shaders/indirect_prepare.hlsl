#include "resource_bindings.hlsl"

[numthreads(256, 1, 1)]
void main(U32 i : SV_DispatchThreadID) {

	if(i >= 2)			//As a test, we always draw 2 objects from the same buffer
		return;

	//Animate color

	if (i == 0) {
		U32 resourceId = getAppData1u(_swapchainCount + EResourceBinding_ConstantColorBufferRW);
		F32x3 col = sin(_time.xxx * F32x3(0.5, 0.25, 0.125)) * 0.5 + 0.5;
		setAtUniform(resourceId, 0, col);
	}

	//Indirect draws

	{
		U32 resourceId = getAppData1u(_swapchainCount + EResourceBinding_IndirectDrawRW);

		IndirectDrawIndexed indirectDraw = (IndirectDrawIndexed) 0;
		indirectDraw.instanceCount = 1;

		switch (i) {

			case 0:
				indirectDraw.indexCount = 3;
				indirectDraw.indexOffset = 6;
				break;

			default:
				indirectDraw.indexCount = 6;
				indirectDraw.indexOffset = 9;
				break;
		}

		setAtUniform(resourceId, i * sizeof(IndirectDrawIndexed), indirectDraw);
	}
}
