#pragma once
#include "types/material.h"

//Transform from tangent space

void Shading_getTBNFromNormal(f32x4 N, f32x4 TBN[3]);
f32x4 Shading_getPerpendicularVector(f32x4 N);

//Brdf helper functions

f32x4 Shading_getF0(struct Material m);
f32x4 Shading_getFresnel(f32x4 F0, f32 VdotN, f32 roughness);
f32 Shading_getSmith(f32 NoR, f32 NoL, f32 rough);
f32 Shading_getSmithMasking(f32 NoR, f32 rough);
f32 Shading_getSmithShadowing(f32 NoR, f32 NoL, f32 rough);

void Shading_redirectRay(struct Ray *r, f32x4 pos, f32x4 gN, f32x4 L);

//Random helper

f32x4 Shading_sampleCosHemisphere(f32x4 xi, f32x4 N, f32 *pdf);
f32x4 Shading_sampleGgxVndf(f32x4 xi, f32x4 V, f32 rough);

//Shading functions

f32x4 Shading_evalSpecular(f32x4 xi, struct Material m, struct Ray *r, f32x4 pos, f32x4 N, f32x4 gN);
f32x4 Shading_evalDiffuse(f32x4 xi, struct Material m, struct Ray *r, f32x4 pos, f32x4 N, f32x4 gN);