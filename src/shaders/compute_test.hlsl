#include "resources.hlsl"

[numthreads(16, 16, 1)]
void main(U32x2 i : SV_DispatchThreadID) {

	U32 resourceId = getAppData1u(_swapchainCount);

	F32x3 col = sin(_time.xxx * F32x3(0.5, 0.25, 0.125)) * 0.5 + 0.5;

	setAtUniform<F32x3>(resourceId, 0, col);

	resourceId = getAppData1u(0) & ResourceId_mask;
	RWTexture2D<unorm F32x4> swapchain = rwTexture2DUniform(resourceId);

	U32x2 wh;
	swapchain.GetDimensions(wh.x, wh.y);

	if(any(i >= wh))
		return;

    swapchain[i] += F32x4(0, (i.x & 15) / 15.0, (i.y & 15) / 15.0, 1);
}
