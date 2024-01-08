#include "resource_bindings.hlsl"

[numthreads(256, 1, 1)]
void main(U32 i : SV_DispatchThreadID) {

	if(i >= 2)			//As a test, we always draw 2 objects from the same buffer
		return;

	//View projection matrices

	if(i == 0) {
	
		F32x3 camPos = F32x3(2, 2, 2);

		F32 aspect = 1;

		if(_swapchainCount) {

			U32 readSwapchain = getReadSwapchain(0);

			if(readSwapchain != U32_MAX) {

				U32x3 dims;
				texture2DUniform(readSwapchain).GetDimensions(0, dims.x, dims.y, dims.z);

				aspect = (float)dims.x / dims.y;
			}
		}

		F32x4x4 v = F32x4x4_lookAt(camPos, 0.xxx, F32x3(0, 1, 0));
		F32x4x4 p = F32x4x4_perspective(90 * F32_degToRad, aspect, 0.1, 100);

		ViewProjMatrices vp;
		vp.view = v;
		vp.proj = p;
		vp.viewProj = mul(v, p);
		
		U32 viewProjMatRW = getAppData1u(EResourceBinding_ViewProjMatricesRW);
		setAtUniform<ViewProjMatrices>(viewProjMatRW, 0, vp);
	}

	//Indirect dispatch

	if (i == 0) {
		U32 resourceId = getAppData1u(EResourceBinding_IndirectDispatchRW);
		setAtUniform(resourceId, 0 * sizeof(IndirectDispatch), I32x3(1, 1, 1));
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
