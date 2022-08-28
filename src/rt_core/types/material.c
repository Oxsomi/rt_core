#include "types/material.h"

struct Material Material_init(
	f32x4 albedo, f32 metallic,
	f32x4 emissive, f32 roughness,
	f32 specular, f32 anisotropy, f32 clearcoat, f32 clearcoatRoughness,
	f32 sheen, f32 sheenTint, f32 subsurface, f32 scatterDistance,
	f32 transparency, f32 translucency, f32 absorptionMultiplier, f32 ior
) {
	return (struct Material) {

		.albedo_metallic = f32x4_init4(
			f32x4_x(albedo), f32x4_y(albedo), f32x4_z(albedo), metallic
		),

		.emissive_roughness = f32x4_init4(
			f32x4_x(emissive), f32x4_y(emissive), f32x4_z(emissive), roughness
		),

		.specular_anisotropy_clearcoat_clearcoatRoughness = f32x4_init4(
			specular, anisotropy, clearcoat, clearcoatRoughness
		),

		.sheen_sheenTint_subsurface_scatterDistance = f32x4_init4(
			sheen, sheenTint, subsurface, scatterDistance
		),

		.transparency_translucency_absorptionMultiplier_ior = f32x4_init4(
			transparency, translucency, absorptionMultiplier, ior
		)
	};
}