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

#include "math/intersections.h"
#include "types/error.h"

void Ray_create(Ray *ray, F32x4 pos, F32 minT, F32x4 dir, F32 maxT) {

	if(!ray)
		return;

	ray->origin = pos;
	ray->dir = dir;
	ray->minT = minT;
	ray->maxT = maxT;
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
		return (Sphere) { 0 };

	return (Sphere) { pos, rad2 };
}

Bool Intersection_check(Intersection *i, Ray r, F32 t, U32 object) {

	if(!i)
		return false;

	Bool beforeHit = i->hitT < 0 || t < i->hitT;

	if (beforeHit && t < r.maxT && t >= r.minT) {
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

	F32x4 f = F32x4_sub(r.origin, s.origin);
	F32 b = -F32x4_dot4(f, r.dir);

	F32 r2 = s.r2;

	F32 D = r2 - F32x4_sqLen4(
		F32x4_add(f, F32x4_mul(
			r.dir,
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
