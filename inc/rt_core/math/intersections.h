#pragma once
#include "types/transform.h"

struct Ray {
	f32x4 originMinT;
	f32x4 dirMaxT;
};

typedef f32x4 Sphere;		//originRadius2

struct AABB {
	f32x4 mi, ma;
};

struct Intersection {
	f32 hitT;
	u32 object;
};

void Ray_init(struct Ray *ray, f32x4 pos, f32 minT, f32x4 dir, f32 maxT);
void Intersection_init(struct Intersection *i);
Sphere Sphere_init(f32x4 pos, f32 rad);

bool Intersection_check(struct Intersection *i, struct Ray r, f32 t, u32 objectId);
bool Sphere_intersect(Sphere s, struct Ray r, struct Intersection *i, u32 objectId);