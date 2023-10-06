#include "../../core3/src/graphics/shaders/resources.hlsl"

[numthreads(16, 16, 1)]
void main(I32x2 i : _globalId) {

	//Assume swapchain texture to be present and unorm for now.

	if(_swapchainCount == 0)
		return;

	uint resourceId = getAppData1u(0) & ResourceId_mask;
    rwTexture2DUniform(resourceId)[i] = F32x4((i.x & 15) / 15.0, (i.y & 15) / 15.0, 0, 1);
}
