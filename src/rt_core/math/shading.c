#include "math/shading.h"
#include "math/intersections.h"

//Helper functions

void Shading_redirectRay(struct Ray *r, F32x4 pos, F32x4 gN, F32x4 L) {

	r->originMinT = Ray_offsetEpsilon(pos, gN);
	F32x4_setW(&r->originMinT, 0);

	r->dirMaxT = L;
	F32x4_setW(&r->dirMaxT, F32_MAX);
}

F32x4 Shading_getF0(struct Material m) {
	return F32x4_lerp(
		F32x4_xxxx4(0.08f * m.specular),
		m.albedo,
		m.metallic
	);
}

F32x4 Shading_getFresnel(F32x4 F0, F32 VoN, F32 roughness) {

	F32x4 F90 = F32x4_xxxx4(1.f - roughness);

	return F32x4_add(
		F0, 
		F32x4_mul(
			F32x4_xxxx4(Math_pow5f(VoN)),
			F32x4_sub(F32x4_max(F0, F90), F0)
		)
	);
}

//https://schuttejoe.github.io/post/ggximportancesamplingpart1/
F32 Shading_getSmithMasking(F32 NoR, F32 rough) {
	F32 a2 = Math_pow2f(rough);
	F32 denom = NoR + Math_sqrtf(a2 + (1 - a2) * Math_pow2f(NoR));
	return 2 * NoR / denom;
}

//https://schuttejoe.github.io/post/ggximportancesamplingpart2/
F32 Shading_getSmithShadowing(F32 NoR, F32 NoL, F32 rough) {

	F32 a2 = Math_pow2f(rough);

	F32 i = NoR * Math_sqrtf(a2 + (1 - a2) * Math_pow2f(NoL));
	F32 o = NoL * Math_sqrtf(a2 + (1 - a2) * Math_pow2f(NoR));

	F32 denom = i + o;

	return 2 * NoR * NoL / denom;
}

F32 Shading_getSmith(F32 NoR, F32 NoL, F32 rough) {
	return
		Shading_getSmithMasking(NoR, rough) /
		Shading_getSmithShadowing(NoR, NoL, rough);
}

//Space transform

F32x4 Shading_getPerpendicularVector(F32x4 N) {

	F32x4 a = F32x4_abs(N);
	Bool xm = (F32x4_x(a) - F32x4_y(a)) < 0 && (F32x4_x(a) - F32x4_z(a)) < 0;
	Bool ym = (F32x4_y(a) - F32x4_z(a)) < 0 ? 1 ^ xm : 0;

	return F32x4_cross3(N, F32x4_init3(xm, ym, (F32)(1 ^ (xm | ym))));
}

void Shading_getTBNFromNormal(F32x4 N, F32x4 TBN[3]) {
	TBN[0] = Shading_getPerpendicularVector(N);
	TBN[1] = F32x4_cross3(TBN[0], N);
	TBN[2] = N;
}

//Random helper

//A cos hemisphere better distributes the samples because the * NoL weighs it
//This fits the shape and thus samples important parts more often
//Taken from wisp: https://github.com/TeamWisp/WispRenderer/blob/master/resources/shaders/rand_util.hlsl

F32x4 Shading_sampleCosHemisphere(F32x4 xi, F32x4 N, F32 *pdf) {

	F32 phi = (2 * Math_pi) * F32x4_x(xi);
	F32 sinTheta = Math_sqrtf(F32x4_y(xi));
	F32 cosTheta = Math_sqrtf(1 - F32x4_y(xi));

	F32x4 H = F32x4_init3(
		sinTheta * Math_cos(phi),
		sinTheta * Math_sin(phi),
		cosTheta
	);

	//Rotate our direction from tangent space to world space

	F32x4 TBN[3];
	Shading_getTBNFromNormal(N, TBN);

	F32x4 L = F32x4_normalize3(F32x4_mul3x3(H, TBN));

	//Calculate PDF and return result

	F32 NoL = F32x4_satDot3(N, L);

	*pdf = NoL / Math_pi;
	return L;
}


//https://schuttejoe.github.io/post/ggximportancesamplingpart2/
//https://hal.archives-ouvertes.fr/hal-01509746/document
//ggx View normal distribution function

F32x4 Shading_sampleGgxVndf(F32x4 xi, F32x4 V, F32 rough) {

	//Pretend to be sampling roughness 1

	F32x4 roughStretch = F32x4_xxy(F32x4_init1(rough));
	F32x4 Vr = F32x4_normalize3(F32x4_mul(F32x4_xzy(V), roughStretch));

	//Point on disk (weighted proportionally to its projection onto Vr)

	F32 a = 1 / (1 + F32x4_z(Vr));
	F32 r = Math_sqrtf(F32x4_x(xi));

	F32 phi = 
		F32x4_y(xi) < a ? 
		Math_pi * (F32x4_y(xi) / a) : 
		Math_pi + Math_pi * (F32x4_y(xi) - a) / (1 - a);

	F32 px = r * Math_cos(phi);
	F32 py = r * Math_sin(phi) * (F32x4_y(xi) < a ? 1 : F32x4_z(Vr));

	F32x4 p = F32x4_init3(
		px,
		py,
		Math_sqrtf(Math_maxf(0, 1 - Math_pow2f(px) - Math_pow2f(py)))
	);

	//Transform tangent to world

	F32x4 TBN[3];
	Shading_getTBNFromNormal(Vr, TBN);

	F32x4 N = F32x4_mul3x3(p, TBN);
	F32x4_setZ(&N, Math_maxf(0, F32x4_z(N)));

	//Unstretch to correct for roughness

	return F32x4_xzy(F32x4_normalize3(F32x4_mul(N, roughStretch)));
}

//Shading

F32x4 Shading_evalSpecular(F32x4 xi, struct Material m, struct Ray *r, F32x4 pos, F32x4 N, F32x4 gN) {

	//Material params

	F32 rough = m.roughness;
	F32x4 F0 = Shading_getF0(m);

	//Calculate L

	F32x4 R = F32x4_xyz(r->dirMaxT);
	F32x4 V = F32x4_negate(R);

	F32x4 L = Shading_sampleGgxVndf(xi, V, rough);
	F32x4 H = F32x4_reflect3(R, L);

	//Shoot new ray using a specular lobe (ggx iso)

	Shading_redirectRay(r, pos, gN, L);

	//Grab terms for lighting

	F32 NoL = F32x4_satDot3(N, L);
	F32 NoR = F32x4_satDot3(N, R);
	F32 LoH = F32x4_satDot3(L, H);

	F32x4 F = Shading_getFresnel(F0, LoH, rough);
	F32 G = Shading_getSmith(NoR, NoL, rough);

	//Simplified BRDF for isotropic
	//Already divided by pdf, which simplifies out the D term

	return F32x4_mul(F, F32x4_xxxx4(G));
}

F32x4 Shading_evalDiffuse(F32x4 xi, struct Material m, struct Ray *r, F32x4 pos, F32x4 N, F32x4 gN) {

	//Calculate L and NoL

	F32 pdf = 0;
	F32x4 L = Shading_sampleCosHemisphere(xi, N, &pdf);

	F32 NoL = F32x4_dot3(N, L);

	if (NoL <= 0 || pdf <= 0) {
		F32x4_setW(&r->originMinT, -1);
		return F32x4_zero();
	}

	F32x4 V = F32x4_negate(F32x4_xyz(r->dirMaxT));

	//Shoot new ray using a diffuse lobe (cos hemi)

	Shading_redirectRay(r, pos, gN, L);

	//Calculate contribution (1 - F) * alb / pi * NdotL / pdf

	F32 VoN = F32x4_satDot3(V, N);

	F32x4 F0 = Shading_getF0(m);
	F32x4 F = Shading_getFresnel(F0, VoN, m.roughness);

	F32x4 dif = F32x4_mul(m.albedo, F32x4_sub(F32x4_one(), F));

	F32 multiplier = 1 / Math_pi * (1 - m.metallic) * (1 - m.translucency) * NoL / pdf;
	return F32x4_mul(dif, F32x4_xxxx4(multiplier));
}
