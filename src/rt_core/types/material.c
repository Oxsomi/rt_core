#include "types/material.h"

struct Material Material_create(
	f32x4 albedo, f32 metallic,
	f32 emissive, f32 roughness,
	f32 specular, f32 anisotropy, f32 clearcoat, f32 clearcoatRoughness,
	f32 sheen, f32 sheenTint, f32 subsurface, f32 scatterDistance,
	f32 transparency, f32 translucency, f32 absorptionMultiplier, f32 ior
) {
	return (struct Material) {

		.albedo = albedo,

		.emissive = emissive,
		.roughness = roughness,
		.metallic = metallic,

		.specular = specular,
		.anisotropy = anisotropy,
		.clearcoat = clearcoat,
		.clearcoatRoughness = clearcoatRoughness,

		.sheen = sheen,
		.sheenTint = sheenTint,
		.subsurface = subsurface,
		.scatterDistance = scatterDistance,

		.transparency = transparency,
		.translucency = translucency,
		.absorptionMultiplier = absorptionMultiplier,
		.ior = ior
	};
}