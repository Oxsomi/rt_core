#include "resources.hlsl"

[numthreads(16, 16, 1)]
void main(U32x2 i : SV_DispatchThreadID) {

	uint resourceId = getAppData1u(0) & ResourceId_mask;
	RWTexture2D<unorm float4> swapchain = rwTexture2DUniform(resourceId);

	uint2 wh;
	swapchain.GetDimensions(wh.x, wh.y);

	if(any(i >= wh))
		return;

    swapchain[i] += F32x4(0, (i.x & 15) / 15.0, (i.y & 15) / 15.0, 1);
}
