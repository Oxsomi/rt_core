/* OxC3/RT Core(Oxsomi core 3/RT Core), a general framework for raytracing applications.
*  Copyright (C) 2023 - 2024 Oxsomi / Nielsbishere (Niels Brunekreef)
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program. If not, see https://github.com/Oxsomi/core3/blob/main/LICENSE.
*  Be aware that GPL3 requires closed source products to be GPL3 too if released to the public.
*  To prevent this a separate license will have to be requested at contact@osomi.net for a premium;
*  This is called dual licensing.
*/

#pragma once
#include "@resources.hlsl"

enum EResourceBinding {

	EResourceBinding_ConstantColorBuffer,
	EResourceBinding_ConstantColorBufferRW,
	EResourceBinding_IndirectDrawRW,
	EResourceBinding_IndirectDispatchRW,

	EResourceBinding_ViewProjMatricesRW,
	EResourceBinding_ViewProjMatrices,
	EResourceBinding_Crabbage2049x,
	EResourceBinding_CrabbageCompressed,

	EResourceBinding_Sampler,
	EResourceBinding_TLAS,
	EResourceBinding_RenderTargetRW,
	EResourceBinding_Padding,

	EResourceBinding_SunDirXYZ,
	EResourceBinding_Padding1 = EResourceBinding_SunDirXYZ + 3,

	EResourceBinding_CamPosXYZ,
	EResourceBinding_Next = EResourceBinding_CamPosXYZ + 3
};

struct ViewProjMatrices {
	F32x4x4 view, proj, viewProj;
	F32x4x4 viewInv, projInv, viewProjInv;
};
