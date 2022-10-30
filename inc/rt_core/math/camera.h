#pragma once
#include "intersections.h"

typedef struct Camera {

	Transform transform;

	F32x4 p0, right, up;

	F32 near, far, fovRad;

} Camera;

struct Camera Camera_create(Quat q, F32x4 pos, F32 fovDeg, F32 near, F32 far, U16 w, U16 h);

Bool Camera_genRay(const Camera *cam, Ray *ray, U16 x, U16 y, U16 w, U16 h, F32 jitterX, F32 jitterY);
