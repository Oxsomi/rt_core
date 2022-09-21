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
	U64 textures[(MaterialTexture_Count + 2) / 3];			//Packed u21[3][2]
	F32x4 albedo;
	F32 emissive, roughness, metallic; U32 flags;
	F32 specular, anisotropy, clearcoat, clearcoatRoughness;
	F32 sheen, sheenTint, subsurface, scatterDistance;
	F32 transparency, translucency, absorptionMultiplier, ior;
};

//Constructors

struct Material Material_create(
	F32x4 albedo, F32 metallic,
	F32 emissive, F32 roughness,
	F32 specular, F32 anisotropy, F32 clearcoat, F32 clearcoatRoughness,
	F32 sheen, F32 sheenTint, F32 subsurface, F32 scatterDistance,
	F32 transparency, F32 translucency, F32 absorptionMultiplier, F32 ior
);

inline struct Material Material_createMetal(
	F32x4 albedo, F32 roughness,
	F32 emissive, F32 anisotropy,
	F32 clearcoat, F32 clearcoatRoughness, F32 transparency
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
	F32x4 albedo, F32 specular,
	F32 emissive, F32 roughness,
	F32 clearcoat, F32 clearcoatRoughness, F32 sheen, F32 sheenTint,
	F32 subsurface, F32 scatterDistance, F32 transparency, F32 translucency,
	F32 absorptionMultiplier, F32 ior
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

inline U32 Material_getTexture(struct Material m, enum MaterialTexture off) { 

	if (off >= MaterialTexture_Count)
		return U32_MAX;		//Out of bounds

	return U64_unpack21x3(m.textures[off / 3], off % 3);
}

inline Bool Material_getFlag(struct Material m, U8 off) { 

	if (off >= 32)
		return false;		//Out of bounds

	return U32_getBit(m.flags, off);
}

//Setters

inline Bool Material_setTexture(struct Material *m, enum MaterialTexture off, U32 textureId) { 

	if (!m || off >= MaterialTexture_Count)
		return false;

	return U64_setPacked21x3(&m->textures[off / 3], off % 3, textureId);
}

inline Bool Material_setFlag(struct Material *m, U8 off, Bool b) {

	if (off >= 32 || !m)
		return false;

	return U32_setBit(&m->flags, off, b);
}
