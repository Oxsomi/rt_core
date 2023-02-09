/* MIT License
*   
*  Copyright (c) 2022 Oxsomi, Nielsbishere (Niels Brunekreef)
*  
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*  
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.
*  
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE. 
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
