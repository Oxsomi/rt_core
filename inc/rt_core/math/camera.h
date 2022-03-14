#pragma once
#include "intersections.h"

struct Camera {

	struct Transform transform;

	f32x4 p0, right, up;

	f32 near, far, fovRad;
};

struct Camera Camera_init(quat q, f32x4 pos, f32 fovDeg, f32 near, f32 far, u16 w, u16 h);

void Camera_genRay(const struct Camera *cam, struct Ray *ray, u16 x, u16 y, u16 w, u16 h, f32 jitterX, f32 jitterY);