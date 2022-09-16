#include "math/camera.h"
#include "math/math.h"
#include "types/assert.h"

struct Camera Camera_create(quat q, f32x4 pos, f32 fovDeg, f32 near, f32 far, u16 w, u16 h) {

	f32 fovRad = fovDeg * Math_degToRad;
	f32 aspect = (f32)w / h;
	
	f32 nearPlaneLeft = Math_tan(fovRad * .5f);

	struct Transform tr = Transform_init(q, pos, f32x4_one());

	f32x4 p0 = Transform_apply(tr, f32x4_init3(-aspect, 1,  -nearPlaneLeft));
	f32x4 p1 = Transform_apply(tr, f32x4_init3(aspect,  1,  -nearPlaneLeft));
	f32x4 p2 = Transform_apply(tr, f32x4_init3(-aspect, -1, -nearPlaneLeft));

	f32x4 right = f32x4_sub(p1, p0);
	f32x4 up = f32x4_sub(p2, p0);

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

void Camera_genRay(const struct Camera *c, struct Ray *ray, u16 x, u16 y, u16 w, u16 h, f32 jitterX, f32 jitterY) {

	ocAssert("Out of bounds", x < w && y < h);

	f32x4 right = f32x4_mul(c->right, f32x4_xxxx4((x + jitterX) / w));
	f32x4 up = f32x4_mul(c->up, f32x4_xxxx4((y + jitterY) / h));

	f32x4 pos = f32x4_add(f32x4_add(c->p0, right), up);
	f32x4 dir = f32x4_normalize3(f32x4_sub(pos, c->transform.pos));

	Ray_create(ray, c->transform.pos, c->near, dir, c->far);
}