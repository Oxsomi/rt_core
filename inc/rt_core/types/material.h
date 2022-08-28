#pragma once
#include "math/vecf.h"
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
	u64 textures[(MaterialTexture_Count + 2) / 3];			//Packed u20[3][2]. The remaining 8 bits are for flags
	f32x4 albedo_metallic;
	f32x4 emissive_roughness;
	f32x4 specular_anisotropy_clearcoat_clearcoatRoughness;
	f32x4 sheen_sheenTint_subsurface_scatterDistance;
	f32x4 transparency_translucency_absorptionMultiplier_ior;
};

//Constructors

struct Material Material_init(
	f32x4 albedo, f32 metallic,
	f32x4 emissive, f32 roughness,
	f32 specular, f32 anisotropy, f32 clearcoat, f32 clearcoatRoughness,
	f32 sheen, f32 sheenTint, f32 subsurface, f32 scatterDistance,
	f32 transparency, f32 translucency, f32 absorptionMultiplier, f32 ior
);

inline struct Material Material_initMetal(
	f32x4 albedo, f32 roughness,
	f32x4 emissive, f32 anisotropy,
	f32 clearcoat, f32 clearcoatRoughness, f32 transparency
) {
	return Material_init(
		albedo, 1,
		emissive, roughness,
		0, anisotropy, clearcoat, clearcoatRoughness, 
		0, 0, 0, 0, 
		transparency, 0, 0, 0
	);
}

inline struct Material Material_initDielectric(
	f32x4 albedo, f32 specular,
	f32x4 emissive, f32 roughness,
	f32 clearcoat, f32 clearcoatRoughness, f32 sheen, f32 sheenTint,
	f32 subsurface, f32 scatterDistance, f32 transparency, f32 translucency,
	f32 absorptionMultiplier, f32 ior
) {
	return Material_init(
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

	return u64_unpack20x3(m.textures[off / 3], off % 3);
}

inline bool Material_getFlag(struct Material m, u8 off) { 

	if (off >= (((MaterialTexture_Count + 2) / 3) << 2))
		return false;		//Out of bounds

	return u64_getBit(m.textures[off >> 2], 60 + (off & 3));
}

inline f32x4 Material_getAlbedo(struct Material m) { return f32x4_xyz(m.albedo_metallic); }
inline f32x4 Material_getEmissive(struct Material m) { return f32x4_xyz(m.emissive_roughness); }

inline f32 Material_getMetallic(struct Material m) { return f32x4_w(m.albedo_metallic); }
inline f32 Material_getRoughness(struct Material m) { return f32x4_w(m.emissive_roughness); }

inline f32 Material_getSpecular(struct Material m) { return f32x4_x(m.specular_anisotropy_clearcoat_clearcoatRoughness); }
inline f32 Material_getAnisotropy(struct Material m) { return f32x4_y(m.specular_anisotropy_clearcoat_clearcoatRoughness); }
inline f32 Material_getClearcoat(struct Material m) { return f32x4_z(m.specular_anisotropy_clearcoat_clearcoatRoughness); }
inline f32 Material_getClearcoatRoughness(struct Material m) { return f32x4_w(m.specular_anisotropy_clearcoat_clearcoatRoughness); }

inline f32 Material_getSheen(struct Material m) { return f32x4_x(m.sheen_sheenTint_subsurface_scatterDistance); }
inline f32 Material_getSheenTint(struct Material m) { return f32x4_y(m.sheen_sheenTint_subsurface_scatterDistance); }
inline f32 Material_getSubsurface(struct Material m) { return f32x4_z(m.sheen_sheenTint_subsurface_scatterDistance); }
inline f32 Material_getScatterDistance(struct Material m) { return f32x4_w(m.sheen_sheenTint_subsurface_scatterDistance); }

inline f32 Material_getTransparency(struct Material m) { return f32x4_x(m.transparency_translucency_absorptionMultiplier_ior); }
inline f32 Material_getTranslucency(struct Material m) { return f32x4_y(m.transparency_translucency_absorptionMultiplier_ior); }
inline f32 Material_getAbsorptionMultiplier(struct Material m) { return f32x4_z(m.transparency_translucency_absorptionMultiplier_ior); }
inline f32 Material_getIor(struct Material m) { return f32x4_w(m.transparency_translucency_absorptionMultiplier_ior); }

//Setters

inline bool Material_setTexture(struct Material *m, enum MaterialTexture off, u32 textureId) { 

	if (!m || off >= MaterialTexture_Count)
		return false;

	return u64_setPacked20x3(&m->textures[off / 3], off % 3, textureId);
}

inline bool Material_setFlag(struct Material *m, u8 off, bool b) {

	if (off >= (((MaterialTexture_Count + 2) / 3) << 2) || !m)
		return false;

	return u64_setBit(&m->textures[off >> 2], 60 + (off & 3), b);
}

inline bool Material_setAlbedo(struct Material *m, f32x4 albedo) { 

	if (!m) 
		return false;

	f32 metal = Material_getMetallic(*m);

	m->albedo_metallic = f32x4_xyz(albedo);
	f32x4_setW(&m->albedo_metallic, metal);
	return true;
}

inline bool Material_setEmissive(struct Material *m, f32x4 emissive) { 

	if (!m) 
		return false;

	f32 rough = Material_getRoughness(*m);

	m->emissive_roughness = f32x4_xyz(emissive);
	f32x4_setW(&m->emissive_roughness, rough);
	return true;
}

inline bool Material_setMetallic(struct Material *m, f32 metallic) {
	
	if (!m)
		return false;

	f32x4_setW(&m->albedo_metallic, metallic);
	return true;
}

inline bool Material_setRoughness(struct Material *m, f32 roughness) { 

	if (!m)
		return false;

	f32x4_setW(&m->emissive_roughness, roughness);
	return true;
}

inline bool Material_setSpecular(struct Material *m, f32 specular) { 

	if (!m)
		return false;

	f32x4_setX(&m->specular_anisotropy_clearcoat_clearcoatRoughness, specular);
	return true;
}

inline bool Material_setAnisotropy(struct Material *m, f32 anisotropy) {

	if (!m)
		return false;

	f32x4_setY(&m->specular_anisotropy_clearcoat_clearcoatRoughness, anisotropy);
	return true;
}

inline bool Material_setClearcoat(struct Material *m, f32 clearcoat) {

	if (!m)
		return false;

	f32x4_setZ(&m->specular_anisotropy_clearcoat_clearcoatRoughness, clearcoat);
	return true;
}

inline bool Material_setClearcoatRoughness(struct Material *m, f32 clearcoatRoughness) {

	if (!m)
		return false;

	f32x4_setW(&m->specular_anisotropy_clearcoat_clearcoatRoughness, clearcoatRoughness);
	return true;
}

inline bool Material_setSheen(struct Material *m, f32 sheen) {

	if (!m)
		return false;

	f32x4_setX(&m->sheen_sheenTint_subsurface_scatterDistance, sheen);
	return true;
}

inline bool Material_setSheenTint(struct Material *m, f32 sheenTint) {

	if (!m)
		return false;

	f32x4_setY(&m->sheen_sheenTint_subsurface_scatterDistance, sheenTint);
	return true;
}

inline bool Material_setSubsurface(struct Material *m, f32 subsurface) {

	if (!m)
		return false;

	f32x4_setZ(&m->sheen_sheenTint_subsurface_scatterDistance, subsurface);
	return true;
}

inline bool Material_setScatterDistance(struct Material *m, f32 scatterDistance) {

	if (!m)
		return false;

	f32x4_setW(&m->sheen_sheenTint_subsurface_scatterDistance, scatterDistance);
	return true;
}

inline bool Material_setTransparency(struct Material *m, f32 transparency) {

	if (!m)
		return false;

	f32x4_setX(&m->transparency_translucency_absorptionMultiplier_ior, transparency);
	return true;
}

inline bool Material_setTranslucency(struct Material *m, f32 translucency) {

	if (!m)
		return false;

	f32x4_setY(&m->transparency_translucency_absorptionMultiplier_ior, translucency);
	return true;
}

inline bool Material_setAbsorptionMultiplier(struct Material *m, f32 absorptionMultiplier) {

	if (!m)
		return false;

	f32x4_setZ(&m->transparency_translucency_absorptionMultiplier_ior, absorptionMultiplier);
	return true;
}

inline bool Material_setIor(struct Material *m, f32 ior) {

	if (!m)
		return false;

	f32x4_setW(&m->transparency_translucency_absorptionMultiplier_ior, ior);
	return true;
}
