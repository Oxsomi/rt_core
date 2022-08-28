#include "math/intersections.h"

void Ray_init(struct Ray *ray, f32x4 pos, f32 minT, f32x4 dir, f32 maxT) {

	f32x4_setW(&pos, minT);
	f32x4_setW(&dir, maxT);

	ray->originMinT = pos;
	ray->dirMaxT = dir;
}

//Offset along a ray (solves self intersection), source:
//http://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.7.pdf (chapter 6)

const f32 origin = 1 / 32.0f;
const f32 floatScale = 1 / 65536.0f;
const f32 intScale = 256.0f;

f32x4 Ray_offsetEpsilon(f32x4 pos, f32x4 gN) {

	//TODO: int cast; not the same as floor with neg numbers
	//		asfloat and asint as well as either Veci or manually writing ops

	i32x4 offI = i32x4_fromF32x4(f32x4_mul(gN, f32x4_xxxx4(intScale)));

	i32x4 delta = i32x4_mul(
		i32x4_fromF32x4(f32x4_add(f32x4_mul(f32x4_lt(pos, f32x4_zero()), f32x4_negTwo()), f32x4_one())), 
		offI
	);

	f32x4 pI = f32x4_bitsI32x4(i32x4_add(i32x4_bitsF32x4(pos), delta));

	f32x4 apos = f32x4_abs(pos);

	if(f32x4_x(apos) < origin) f32x4_setX(&pI, f32x4_x(pos) + floatScale * f32x4_x(gN));
	if(f32x4_y(apos) < origin) f32x4_setY(&pI, f32x4_y(pos) + floatScale * f32x4_y(gN));
	if(f32x4_z(apos) < origin) f32x4_setZ(&pI, f32x4_z(pos) + floatScale * f32x4_z(gN));

	return pI;
}

void Intersection_init(struct Intersection *i) {
	i->hitT = -1;
	i->object = u32_MAX;
}

Sphere Sphere_init(f32x4 pos, f32 rad) {
	f32x4_setW(&pos, Math_pow2f(rad));
	return pos;
}

bool Intersection_check(struct Intersection *i, struct Ray r, f32 t, u32 object) {

	bool beforeHit = i->hitT < 0 || t < i->hitT;

	if (beforeHit && t < f32x4_w(r.dirMaxT) && t >= f32x4_w(r.originMinT)) {
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

	f32x4 f = f32x4_sub(r.originMinT, s);
	f32 b = -f32x4_dot3(f, r.dirMaxT);

	f32 r2 = f32x4_w(s);

	f32 D = r2 - f32x4_sqLen3(
		f32x4_add(f, f32x4_mul(
			r.dirMaxT,
			f32x4_xxxx4(b)
		))
	);

	//No intersection, skip

	if (D < 0)
		return false;

	//One intersection

	if (D == 0)
		return Intersection_check(i, r, b, objectId);

	//Two intersections

	f32 c = f32x4_sqLen3(f) - r2;
	f32 q = b + Math_signInc(b) * Math_sqrtf(D);

	f32 o0 = c / q;
	f32 o1 = q;

	//c < 0 means we start inside the sphere, so we only have to test o1

	if (c < 0)
		return Intersection_check(i, r, o1, objectId);

	//Otherwise o0 is the first we hit

	return Intersection_check(i, r, o0, objectId);
}