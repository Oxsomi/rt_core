#include "resources.hlsl"

[numthreads(16, 16, 1)]
void main(U32x2 i : SV_DispatchThreadID) {

	if(!_swapchainCount)
		return;

	U32 writeSwapchain = getWriteSwapchain(0);

	if(writeSwapchain == U32_MAX)
		return;

	RWTexture2D<unorm F32x4> swapchain = rwTexture2DUniform(writeSwapchain);

	U32x2 wh;
	swapchain.GetDimensions(wh.x, wh.y);

	if(any(i >= wh))
		return;

    swapchain[i] = F32x4(0, (i.x & 15) / 15.0, (i.y & 15) / 15.0, 1);
}
