#pragma once
#include "math/transform.h"

struct Ray {
	F32x4 originMinT;
	F32x4 dirMaxT;
};

typedef F32x4 Sphere;		//originRadius2

struct AABB {
	F32x4 mi, ma;
};

struct Intersection {
	F32 hitT;
	U32 object;
};

void Ray_create(struct Ray *ray, F32x4 pos, F32 minT, F32x4 dir, F32 maxT);
F32x4 Ray_offsetEpsilon(F32x4 pos, F32x4 gN);

void Intersection_create(struct Intersection *i);
Sphere Sphere_create(F32x4 pos, F32 rad);

Bool Intersection_check(struct Intersection *i, struct Ray r, F32 t, U32 objectId);
Bool Sphere_intersect(Sphere s, struct Ray r, struct Intersection *i, U32 objectId);