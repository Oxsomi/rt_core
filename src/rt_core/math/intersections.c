#include "math/intersections.h"

void Ray_create(struct Ray *ray, F32x4 pos, F32 minT, F32x4 dir, F32 maxT) {

	F32x4_setW(&pos, minT);
	F32x4_setW(&dir, maxT);

	ray->originMinT = pos;
	ray->dirMaxT = dir;
}

//Offset along a ray (solves self intersection), source:
//http://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.7.pdf (chapter 6)

const F32 origin = 1 / 32.0f;
const F32 floatScale = 1 / 65536.0f;
const F32 intScale = 256.0f;

F32x4 Ray_offsetEpsilon(F32x4 pos, F32x4 gN) {

	//TODO: int cast; not the same as floor with neg numbers
	//		asfloat and asint as well as either Veci or manually writing ops

	I32x4 offI = I32x4_fromF32x4(F32x4_mul(gN, F32x4_xxxx4(intScale)));

	I32x4 delta = I32x4_mul(
		I32x4_fromF32x4(F32x4_add(F32x4_mul(F32x4_lt(pos, F32x4_zero()), F32x4_negTwo()), F32x4_one())), 
		offI
	);

	F32x4 pI = F32x4_bitsI32x4(I32x4_add(I32x4_bitsF32x4(pos), delta));

	F32x4 apos = F32x4_abs(pos);

	if(F32x4_x(apos) < origin) F32x4_setX(&pI, F32x4_x(pos) + floatScale * F32x4_x(gN));
	if(F32x4_y(apos) < origin) F32x4_setY(&pI, F32x4_y(pos) + floatScale * F32x4_y(gN));
	if(F32x4_z(apos) < origin) F32x4_setZ(&pI, F32x4_z(pos) + floatScale * F32x4_z(gN));

	return pI;
}

void Intersection_create(struct Intersection *i) {
	i->hitT = -1;
	i->object = U32_MAX;
}

Sphere Sphere_create(F32x4 pos, F32 rad) {
	F32x4_setW(&pos, Math_pow2f(rad));
	return pos;
}

Bool Intersection_check(struct Intersection *i, struct Ray r, F32 t, U32 object) {

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

Bool Sphere_intersect(Sphere s, struct Ray r, struct Intersection *i, U32 objectId) {

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
	F32 q = b + Math_signInc(b) * Math_sqrtf(D);

	F32 o0 = c / q;
	F32 o1 = q;

	//c < 0 means we start inside the sphere, so we only have to test o1

	if (c < 0)
		return Intersection_check(i, r, o1, objectId);

	//Otherwise o0 is the first we hit

	return Intersection_check(i, r, o0, objectId);
}