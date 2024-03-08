#include "resource_bindings.hlsl"

[numthreads(16, 16, 1)]
void main(U32x2 i : SV_DispatchThreadID) {

	RWTexture2D<unorm F32x4> tex = rwTexture2DUniform(getAppData1u(EResourceBinding_RenderTargetRW));

	U32x2 dims; 
	tex.GetDimensions(dims.x, dims.y);

	if(any(i >= dims))
		return;

	//Generate primaries
	
	U32 viewProjMat = getAppData1u(EResourceBinding_ViewProjMatrices);
	ViewProjMatrices mats = getAtUniform<ViewProjMatrices>(viewProjMat, 0);

	F32x2 uv = (F32x2(i) + 0.5) / F32x2(dims);

	F32x3 eye = mul(F32x4(0.xxx, 1), mats.viewInv).xyz;

	F32x3 rayOrigin = mul(F32x4(uv * 2 - 1, 0, 1), mats.viewProjInv).xyz;
	F32x3 rayDest = mul(F32x4(uv * 2 - 1, 1, 1), mats.viewProjInv).xyz;
	F32x3 rayDir = normalize(rayDest - eye);

	//Trace against

	U32 tlasId = getAppData1u(EResourceBinding_TLAS);

	F32x3 color = rayDir * 0.5 + 0.5;		//Miss

	if(tlasId) {

		RayQuery<
			//RAY_FLAG_CULL_NON_OPAQUE | 
			//RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | 
			RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
		> query;

		//RayDesc ray = { eye, 0, rayDir, 1e6 };
		
		RayDesc ray = { F32x3(-5, uv * 10 - 5), 0, F32x3(1, 0, 0), 1e38 };
		query.TraceRayInline(tlasExtUniform(tlasId), RAY_FLAG_NONE, -1, ray);
		query.Proceed();

		//Triangle hit
		
		if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
			color = F32x3(1, 0, 0);

		else color = F32x3(0.25, 0.5, 1);
	}

	tex[i] = F32x4(color, 1);
}
