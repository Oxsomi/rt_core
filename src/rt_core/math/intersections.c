#include "math/intersections.h"

void Ray_init(struct Ray *ray, f32x4 pos, f32 minT, f32x4 dir, f32 maxT) {

	Vec_setW(&pos, minT);
	Vec_setW(&dir, maxT);

	ray->originMinT = pos;
	ray->dirMaxT = dir;
}

void Intersection_init(struct Intersection *i) {
	i->hitT = -1;
	i->object = u32_MAX;
}

Sphere Sphere_init(f32x4 pos, f32 rad) {
	Vec_setW(&pos, Math_pow2f(rad));
	return pos;
}

bool Intersection_check(struct Intersection *i, struct Ray r, f32 t, u32 object) {

	bool beforeHit = i->hitT < 0 || t < i->hitT;

	if (beforeHit && t < Vec_w(r.dirMaxT) && t >= Vec_w(r.originMinT)) {
		i->hitT = t;
		i->object = object;
		return true;
	}

	return false;
}

//Intersect a sphere
//https://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.9.pdf Chapter 7

bool Sphere_intersect(Sphere s, struct Ray r, struct Intersection *i, u32 objectId) {

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

	f32x4 f = Vec_sub(r.originMinT, s);
	f32 b = -Vec_dot3(f, r.dirMaxT);

	f32 r2 = Vec_w(s);

	f32 D = r2 - Vec_sqLen3(
		Vec_add(f, Vec_mul(
			r.dirMaxT,
			Vec_xxxx4(b)
		))
	);

	//No intersection, skip

	if (D < 0)
		return false;

	//One intersection

	if (D == 0)
		return Intersection_check(i, r, b, objectId);

	//Two intersections

	f32 c = Vec_sqLen3(f) - r2;
	f32 q = b + Math_signInc(b) * Math_sqrtf(D);

	f32 o0 = c / q;
	f32 o1 = q;

	//c < 0 means we start inside the sphere, so we only have to test o1

	if (c < 0)
		return Intersection_check(i, r, o1, objectId);

	//Otherwise o0 is the first we hit

	return Intersection_check(i, r, o0, objectId);
}