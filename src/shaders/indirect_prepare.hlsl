#include "resource_bindings.hlsl"

[numthreads(256, 1, 1)]
void main(U32 i : SV_DispatchThreadID) {

	if(i >= 2)			//As a test, we always draw 2 objects from the same buffer
		return;

	//Indirect dispatch

	if (i == 0) {
		U32 resourceId = getAppData1u(EResourceBinding_IndirectDispatchRW);
		setAtUniform(resourceId, 0 * sizeof(Dispatch), I32x3(1, 1, 1));
	}

	//Indirect draws

	{
		U32 resourceId = getAppData1u(EResourceBinding_IndirectDrawRW);

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
