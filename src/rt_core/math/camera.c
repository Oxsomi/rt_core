/* OxC3(Oxsomi core 3), a general framework and toolset for cross platform applications.
*  Copyright (C) 2023 Oxsomi / Nielsbishere (Niels Brunekreef)
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

#include "math/camera.h"
#include "math/math.h"

Camera Camera_create(
	Quat q, 
	F32x4 pos, 
	F32 fovDeg, 
	F32 near, 
	F32 far, 
	U16 w, 
	U16 h
) {

	F32 fovRad = fovDeg * F32_DEG_TO_RAD;
	F32 aspect = (F32)w / h;
	
	F32 nearPlaneLeft = F32_tan(fovRad * .5f);

	Transform tr = Transform_create(q, pos, F32x4_one());

	F32x4 p0 = Transform_apply(tr, F32x4_create3(-aspect, 1,  -nearPlaneLeft));
	F32x4 p1 = Transform_apply(tr, F32x4_create3(aspect,  1,  -nearPlaneLeft));
	F32x4 p2 = Transform_apply(tr, F32x4_create3(-aspect, -1, -nearPlaneLeft));

	F32x4 right = F32x4_sub(p1, p0);
	F32x4 up = F32x4_sub(p2, p0);

	return (Camera) {
		.transform = tr,
		.near = near,
		.far = far,
		.fovRad = fovRad,
		.p0 = p0,
		.right = right,
		.up = up
	};
}

Bool Camera_genRay(
	const Camera *c, 
	Ray *ray, 
	U16 x, 
	U16 y, 
	U16 w, 
	U16 h, 
	F32 jitterX, 
	F32 jitterY
) {

	if(x >= w || y >= h || !c || !ray)
		return false;

	F32x4 right = F32x4_mul(c->right, F32x4_xxxx4((x + jitterX) / w));
	F32x4 up = F32x4_mul(c->up, F32x4_xxxx4((y + jitterY) / h));

	F32x4 pos = F32x4_add(F32x4_add(c->p0, right), up);
	F32x4 dir = F32x4_normalize3(F32x4_sub(pos, c->transform.pos));

	Ray_create(ray, c->transform.pos, c->near, dir, c->far);
	return true;
}
