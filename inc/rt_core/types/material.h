#pragma once
#include "math/vec.h"
#include "types/pack.h"

//https://google.github.io/filament/Material%20Properties.pdf

enum MaterialTexture {
	MaterialTexture_Albedo,
	MaterialTexture_Alpha,
	MaterialTexture_MetallicRoughness,
	MaterialTexture_Normal,
	MaterialTexture_Specular,
	MaterialTexture_Emissive,
	MaterialTexture_Count
};

struct Material {
	u64 textures[(MaterialTexture_Count + 2) / 3];			//Packed u21[3][2]
	f32x4 albedo;
	f32 emissive, roughness, metallic; u32 flags;
	f32 specular, anisotropy, clearcoat, clearcoatRoughness;
	f32 sheen, sheenTint, subsurface, scatterDistance;
	f32 transparency, translucency, absorptionMultiplier, ior;
};

//Constructors

struct Material Material_create(
	f32x4 albedo, f32 metallic,
	f32 emissive, f32 roughness,
	f32 specular, f32 anisotropy, f32 clearcoat, f32 clearcoatRoughness,
	f32 sheen, f32 sheenTint, f32 subsurface, f32 scatterDistance,
	f32 transparency, f32 translucency, f32 absorptionMultiplier, f32 ior
);

inline struct Material Material_createMetal(
	f32x4 albedo, f32 roughness,
	f32 emissive, f32 anisotropy,
	f32 clearcoat, f32 clearcoatRoughness, f32 transparency
) {
	return Material_create(
		albedo, 1,
		emissive, roughness,
		0, anisotropy, clearcoat, clearcoatRoughness, 
		0, 0, 0, 0, 
		transparency, 0, 0, 0
	);
}

inline struct Material Material_createDielectric(
	f32x4 albedo, f32 specular,
	f32 emissive, f32 roughness,
	f32 clearcoat, f32 clearcoatRoughness, f32 sheen, f32 sheenTint,
	f32 subsurface, f32 scatterDistance, f32 transparency, f32 translucency,
	f32 absorptionMultiplier, f32 ior
) {
	return Material_create(
		albedo, 0,
		emissive, roughness,
		specular, 0, clearcoat, clearcoatRoughness,
		sheen, sheenTint, subsurface, scatterDistance,
		transparency, translucency, absorptionMultiplier, ior
	);
}

//Getters

inline u32 Material_getTexture(struct Material m, enum MaterialTexture off) { 

	if (off >= MaterialTexture_Count)
		return u32_MAX;		//Out of bounds

	return u64_unpack21x3(m.textures[off / 3], off % 3);
}

inline bool Material_getFlag(struct Material m, u8 off) { 

	if (off >= 32)
		return false;		//Out of bounds

	return u32_getBit(m.flags, off);
}

//Setters

inline bool Material_setTexture(struct Material *m, enum MaterialTexture off, u32 textureId) { 

	if (!m || off >= MaterialTexture_Count)
		return false;

	return u64_setPacked21x3(&m->textures[off / 3], off % 3, textureId);
}

inline bool Material_setFlag(struct Material *m, u8 off, bool b) {

	if (off >= 32 || !m)
		return false;

	return u32_setBit(&m->flags, off, b);
}
