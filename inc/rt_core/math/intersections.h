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

#pragma once
#include "math/transform.h"

typedef struct Ray {
	F32x4 originMinT;
	F32x4 dirMaxT;
} Ray;

typedef F32x4 Sphere;		//originRadius2

typedef struct AABB {
	F32x4 mi, ma;
} AABB;

typedef struct Intersection {
	F32 hitT;
	U32 object;
} Intersection;

void Ray_create(
	Ray *ray, 
	F32x4 pos, 
	F32 minT, 
	F32x4 dir, 
	F32 maxT
);

void Intersection_create(Intersection *i);
Sphere Sphere_create(F32x4 pos, F32 rad);

Bool Intersection_check(Intersection *i, Ray r, F32 t, U32 objectId);
Bool Sphere_intersect(Sphere s, Ray r, Intersection *i, U32 objectId);
