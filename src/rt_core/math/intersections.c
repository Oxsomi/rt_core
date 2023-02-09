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

#include "math/intersections.h"
#include "types/error.h"

void Ray_create(Ray *ray, F32x4 pos, F32 minT, F32x4 dir, F32 maxT) {

	if(!ray)
		return;

	F32x4_setW(&pos, minT);
	F32x4_setW(&dir, maxT);

	ray->originMinT = pos;
	ray->dirMaxT = dir;
}

void Intersection_create(Intersection *i) {

	if(!i)
		return;

	i->hitT = -1;
	i->object = U32_MAX;
}

Sphere Sphere_create(F32x4 pos, F32 rad) {

	F32 rad2 = 0;
	Error err = F32_pow2(rad, &rad2);

	if(err.genericError)
		return F32x4_zero();

	F32x4_setW(&pos, rad2);
	return pos;
}

Bool Intersection_check(Intersection *i, Ray r, F32 t, U32 object) {

	if(!i)
		return false;

	Bool beforeHit = i->hitT < 0 || t < i->hitT;

	if (beforeHit && t < F32x4_w(r.dirMaxT) && t >= F32x4_w(r.originMinT)) {
		i->hitT = t;
		i->object = object;
		return true;
	}

	return false;
}

//Intersect a sphere
//https://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.9.pdf Chapter 7

Bool Sphere_intersect(Sphere s, Ray r, Intersection *i, U32 objectId) {

	if(!i)
		return false;

	//Sphere: (P - G) * (P - G) = r^2
	//Ray: P = ro + rd * t

	//Replace P in Sphere with Ray
	//(rd * t + ro - G) . (rd * t + ro - G) = r^2

	//f = ro - G
	//(rd . rd)t^2 + (f . d)2t + f.f - r^2

	//a = rd . rd
	//b = (f . rd) * 2
	//c = f . f - r^2 

	//Gives us (-b +- sqrt(D)) / (2 * a)
	//D = b^2 - 4ac
	//But that has precision issues

	//Better one; 4 * d^2 * (r^2 - (f - (f.d)d)^2)

	F32x4 f = F32x4_sub(r.originMinT, s);
	F32 b = -F32x4_dot3(f, r.dirMaxT);

	F32 r2 = F32x4_w(s);

	F32 D = r2 - F32x4_sqLen3(
		F32x4_add(f, F32x4_mul(
			r.dirMaxT,
			F32x4_xxxx4(b)
		))
	);

	//No intersection, skip

	if (D < 0)
		return false;

	//One intersection

	if (D == 0)
		return Intersection_check(i, r, b, objectId);

	//Two intersections

	F32 c = F32x4_sqLen3(f) - r2;
	F32 q = b + F32_signInc(b) * F32_sqrt(D);

	F32 o0 = c / q;
	F32 o1 = q;

	//c < 0 means we start inside the sphere, so we only have to test o1

	if (c < 0)
		return Intersection_check(i, r, o1, objectId);

	//Otherwise o0 is the first we hit

	return Intersection_check(i, r, o0, objectId);
}
