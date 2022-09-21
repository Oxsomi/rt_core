#pragma once
#include "types/material.h"
#include "intersections.h"

//Transform from tangent space

void Shading_getTBNFromNormal(F32x4 N, F32x4 TBN[3]);
F32x4 Shading_getPerpendicularVector(F32x4 N);

//Brdf helper functions

F32x4 Shading_getF0(struct Material m);
F32x4 Shading_getFresnel(F32x4 F0, F32 VdotN, F32 roughness);
F32 Shading_getSmith(F32 NoR, F32 NoL, F32 rough);
F32 Shading_getSmithMasking(F32 NoR, F32 rough);
F32 Shading_getSmithShadowing(F32 NoR, F32 NoL, F32 rough);

void Shading_redirectRay(struct Ray *r, F32x4 pos, F32x4 gN, F32x4 L);

//Random helper

F32x4 Shading_sampleCosHemisphere(F32x4 xi, F32x4 N, F32 *pdf);
F32x4 Shading_sampleGgxVndf(F32x4 xi, F32x4 V, F32 rough);

//Shading functions

F32x4 Shading_evalSpecular(F32x4 xi, struct Material m, struct Ray *r, F32x4 pos, F32x4 N, F32x4 gN);
F32x4 Shading_evalDiffuse(F32x4 xi, struct Material m, struct Ray *r, F32x4 pos, F32x4 N, F32x4 gN);