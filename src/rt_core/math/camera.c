#include "math/camera.h"
#include "types/math_helper.h"
#include "types/assert_helper.h"

struct Camera Camera_init(quat q, f32x4 pos, f32 fovDeg, f32 near, f32 far, u16 w, u16 h) {

	f32 fovRad = fovDeg * Math_degToRad;
	f32 aspect = (f32)w / h;
	
	f32 nearPlaneLeft = Math_tan(fovRad * .5f);

	struct Transform tr = Transform_init(q, pos, Vec_one());

	f32x4 p0 = Transform_apply(tr, Vec_init3(-aspect, 1,  -nearPlaneLeft));
	f32x4 p1 = Transform_apply(tr, Vec_init3(aspect,  1,  -nearPlaneLeft));
	f32x4 p2 = Transform_apply(tr, Vec_init3(-aspect, -1, -nearPlaneLeft));

	f32x4 right = Vec_sub(p1, p0);
	f32x4 up = Vec_sub(p2, p0);

	return (struct Camera) {
		.transform = tr,
		.near = near,
		.far = far,
		.fovRad = fovRad,
		.p0 = p0,
		.right = right,
		.up = up
	};
}

void Camera_genRay(const struct Camera *c, struct Ray *ray, u16 x, u16 y, u16 w, u16 h) {

	ocAssert("Out of bounds", x < w && y < h);

	f32x4 right = Vec_mul(c->right, Vec_xxxx4((x + .5f) / w));
	f32x4 up = Vec_mul(c->up, Vec_xxxx4((y + .5f) / h));

	f32x4 pos = Vec_add(Vec_add(c->p0, right), up);
	f32x4 dir = Vec_normalize3(Vec_sub(pos, c->transform.pos));

	Ray_init(ray, c->transform.pos, c->near, dir, c->far);
}