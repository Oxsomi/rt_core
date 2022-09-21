#include "types/material.h"

struct Material Material_create(
	F32x4 albedo, F32 metallic,
	F32 emissive, F32 roughness,
	F32 specular, F32 anisotropy, F32 clearcoat, F32 clearcoatRoughness,
	F32 sheen, F32 sheenTint, F32 subsurface, F32 scatterDistance,
	F32 transparency, F32 translucency, F32 absorptionMultiplier, F32 ior
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