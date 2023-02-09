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
