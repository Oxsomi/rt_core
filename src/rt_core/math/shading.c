#include "math/shading.h"
#include "math/intersections.h"

//Helper functions

void Shading_redirectRay(struct Ray *r, f32x4 pos, f32x4 gN, f32x4 L) {

	r->originMinT = Ray_offsetEpsilon(pos, gN);
	f32x4_setW(&r->originMinT, 0);

	r->dirMaxT = L;
	f32x4_setW(&r->dirMaxT, f32_MAX);
}

f32x4 Shading_getF0(struct Material m) {
	return f32x4_lerp(
		f32x4_xxxx4(0.08f * m.specular),
		m.albedo,
		m.metallic
	);
}

f32x4 Shading_getFresnel(f32x4 F0, f32 VoN, f32 roughness) {

	f32x4 F90 = f32x4_xxxx4(1.f - roughness);

	return f32x4_add(
		F0, 
		f32x4_mul(
			f32x4_xxxx4(Math_pow5f(VoN)),
			f32x4_sub(f32x4_max(F0, F90), F0)
		)
	);
}

//https://schuttejoe.github.io/post/ggximportancesamplingpart1/
f32 Shading_getSmithMasking(f32 NoR, f32 rough) {
	f32 a2 = Math_pow2f(rough);
	f32 denom = NoR + Math_sqrtf(a2 + (1 - a2) * Math_pow2f(NoR));
	return 2 * NoR / denom;
}

//https://schuttejoe.github.io/post/ggximportancesamplingpart2/
f32 Shading_getSmithShadowing(f32 NoR, f32 NoL, f32 rough) {

	f32 a2 = Math_pow2f(rough);

	f32 i = NoR * Math_sqrtf(a2 + (1 - a2) * Math_pow2f(NoL));
	f32 o = NoL * Math_sqrtf(a2 + (1 - a2) * Math_pow2f(NoR));

	f32 denom = i + o;

	return 2 * NoR * NoL / denom;
}

f32 Shading_getSmith(f32 NoR, f32 NoL, f32 rough) {
	return
		Shading_getSmithMasking(NoR, rough) /
		Shading_getSmithShadowing(NoR, NoL, rough);
}

//Space transform

f32x4 Shading_getPerpendicularVector(f32x4 N) {

	f32x4 a = f32x4_abs(N);
	bool xm = (f32x4_x(a) - f32x4_y(a)) < 0 && (f32x4_x(a) - f32x4_z(a)) < 0;
	bool ym = (f32x4_y(a) - f32x4_z(a)) < 0 ? 1 ^ xm : 0;

	return f32x4_cross3(N, f32x4_init3(xm, ym, (f32)(1 ^ (xm | ym))));
}

void Shading_getTBNFromNormal(f32x4 N, f32x4 TBN[3]) {
	TBN[0] = Shading_getPerpendicularVector(N);
	TBN[1] = f32x4_cross3(TBN[0], N);
	TBN[2] = N;
}

//Random helper

//A cos hemisphere better distributes the samples because the * NoL weighs it
//This fits the shape and thus samples important parts more often
//Taken from wisp: https://github.com/TeamWisp/WispRenderer/blob/master/resources/shaders/rand_util.hlsl

f32x4 Shading_sampleCosHemisphere(f32x4 xi, f32x4 N, f32 *pdf) {

	f32 phi = (2 * Math_pi) * f32x4_x(xi);
	f32 sinTheta = Math_sqrtf(f32x4_y(xi));
	f32 cosTheta = Math_sqrtf(1 - f32x4_y(xi));

	f32x4 H = f32x4_init3(
		sinTheta * Math_cos(phi),
		sinTheta * Math_sin(phi),
		cosTheta
	);

	//Rotate our direction from tangent space to world space

	f32x4 TBN[3];
	Shading_getTBNFromNormal(N, TBN);

	f32x4 L = f32x4_normalize3(f32x4_mul3x3(H, TBN));

	//Calculate PDF and return result

	f32 NoL = f32x4_satDot3(N, L);

	*pdf = NoL / Math_pi;
	return L;
}


//https://schuttejoe.github.io/post/ggximportancesamplingpart2/
//https://hal.archives-ouvertes.fr/hal-01509746/document
//ggx View normal distribution function

f32x4 Shading_sampleGgxVndf(f32x4 xi, f32x4 V, f32 rough) {

	//Pretend to be sampling roughness 1

	f32x4 roughStretch = f32x4_xxy(f32x4_init1(rough));
	f32x4 Vr = f32x4_normalize3(f32x4_mul(f32x4_xzy(V), roughStretch));

	//Point on disk (weighted proportionally to its projection onto Vr)

	f32 a = 1 / (1 + f32x4_z(Vr));
	f32 r = Math_sqrtf(f32x4_x(xi));

	f32 phi = 
		f32x4_y(xi) < a ? 
		Math_pi * (f32x4_y(xi) / a) : 
		Math_pi + Math_pi * (f32x4_y(xi) - a) / (1 - a);

	f32 px = r * Math_cos(phi);
	f32 py = r * Math_sin(phi) * (f32x4_y(xi) < a ? 1 : f32x4_z(Vr));

	f32x4 p = f32x4_init3(
		px,
		py,
		Math_sqrtf(Math_maxf(0, 1 - Math_pow2f(px) - Math_pow2f(py)))
	);

	//Transform tangent to world

	f32x4 TBN[3];
	Shading_getTBNFromNormal(Vr, TBN);

	f32x4 N = f32x4_mul3x3(p, TBN);
	f32x4_setZ(&N, Math_maxf(0, f32x4_z(N)));

	//Unstretch to correct for roughness

	return f32x4_xzy(f32x4_normalize3(f32x4_mul(N, roughStretch)));
}

//Shading

f32x4 Shading_evalSpecular(f32x4 xi, struct Material m, struct Ray *r, f32x4 pos, f32x4 N, f32x4 gN) {

	//Material params

	f32 rough = m.roughness;
	f32x4 F0 = Shading_getF0(m);

	//Calculate L

	f32x4 R = f32x4_xyz(r->dirMaxT);
	f32x4 V = f32x4_negate(R);

	f32x4 L = Shading_sampleGgxVndf(xi, V, rough);
	f32x4 H = f32x4_reflect3(R, L);

	//Shoot new ray using a specular lobe (ggx iso)

	Shading_redirectRay(r, pos, gN, L);

	//Grab terms for lighting

	f32 NoL = f32x4_satDot3(N, L);
	f32 NoR = f32x4_satDot3(N, R);
	f32 LoH = f32x4_satDot3(L, H);

	f32x4 F = Shading_getFresnel(F0, LoH, rough);
	f32 G = Shading_getSmith(NoR, NoL, rough);

	//Simplified BRDF for isotropic
	//Already divided by pdf, which simplifies out the D term

	return f32x4_mul(F, f32x4_xxxx4(G));
}

f32x4 Shading_evalDiffuse(f32x4 xi, struct Material m, struct Ray *r, f32x4 pos, f32x4 N, f32x4 gN) {

	//Calculate L and NoL

	f32 pdf = 0;
	f32x4 L = Shading_sampleCosHemisphere(xi, N, &pdf);

	f32 NoL = f32x4_dot3(N, L);

	if (NoL <= 0 || pdf <= 0) {
		f32x4_setW(&r->originMinT, -1);
		return f32x4_zero();
	}

	f32x4 V = f32x4_negate(f32x4_xyz(r->dirMaxT));

	//Shoot new ray using a diffuse lobe (cos hemi)

	Shading_redirectRay(r, pos, gN, L);

	//Calculate contribution (1 - F) * alb / pi * NdotL / pdf

	f32 VoN = f32x4_satDot3(V, N);

	f32x4 F0 = Shading_getF0(m);
	f32x4 F = Shading_getFresnel(F0, VoN, m.roughness);

	f32x4 dif = f32x4_mul(m.albedo, f32x4_sub(f32x4_one(), F));

	f32 multiplier = 1 / Math_pi * (1 - m.metallic) * (1 - m.translucency) * NoL / pdf;
	return f32x4_mul(dif, f32x4_xxxx4(multiplier));
}
