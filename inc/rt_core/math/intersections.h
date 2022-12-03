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
