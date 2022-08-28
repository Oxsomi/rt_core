#include "math/shading.h"
#include "math/camera.h"
#include "math/rand.h"
#include "types/bit.h"
#include "types/timer.h"
#include "types/assert.h"
#include "formats/bmp.h"
#include "file/file.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include <stdio.h>

void *ourAlloc(void *allocator, usz siz) {
	allocator;
	return malloc(siz);
}

void ourFree(void *allocator, struct Buffer buf) {
	allocator;
	free(buf.ptr);
}

//Handling multi threaded tracing

struct RaytracingThread {

	struct Thread *thread;

	struct Buffer imageBuf;
	const struct Camera *cam;

	const Sphere *spheres;

	u32 sphereCount;

	u16 yOffset, ySize;
	u16 w, h;

	u32 skyColor;

	const struct Material *materials;
	u32 materialCount;

	const u64 *packedMaterialIds;
};

#define SUPER_SAMPLING 1
#define MAX_BOUNCES_U8 12

void trace(struct RaytracingThread *rtThread) {

	struct Ray r;
	struct Intersection inter;

	const struct Camera *c = rtThread->cam;
	struct Buffer *buf = &rtThread->imageBuf;

	u16 w = rtThread->w, h = rtThread->h;
	u32 frameId = 0;

	u8 superSamp = SUPER_SAMPLING, superSamp2;

	if (((w * SUPER_SAMPLING) < w) || ((h * SUPER_SAMPLING) < h))
		superSamp = 1;

	superSamp2 = superSamp * superSamp;

	for (u16 j = rtThread->yOffset, jMax = j + rtThread->ySize; j < jMax; ++j)
		for (u16 i = 0; i < w; ++i) {

			f32x4 accum = f32x4_zero();

			for(u16 jj = 0; jj < superSamp; ++jj)
				for(u16 ii = 0; ii < superSamp; ++ii) {

					u32 seed = Random_seed(
						i * superSamp + ii, j * superSamp+ jj, 
						w * superSamp,
						frameId
					);

					Camera_genRay(c, &r, i, j, w, h, ii / (f32)superSamp, jj / (f32)superSamp);

					Intersection_init(&inter);

					f32x4 contrib = f32x4_one();
					f32x4 col = f32x4_zero();

					for (u16 k = 0; k < 1 + (u16)MAX_BOUNCES_U8; ++k) {

						//Get intersection

						for (u32 l = 0; l < rtThread->sphereCount; ++l)
							Sphere_intersect(rtThread->spheres[l], r, &inter, l);

						//

						if (inter.hitT >= 0) {

							//Process intersection

							f32x4 pos = f32x4_add(r.originMinT, f32x4_mul(r.dirMaxT, f32x4_xxxx4(inter.hitT)));
							f32x4 nrm = f32x4_normalize3(f32x4_sub(pos, rtThread->spheres[inter.object]));

							//Grab material

							u32 materialId = u64_unpack21x3(rtThread->packedMaterialIds[inter.object / 3], inter.object % 3);

							if (materialId < rtThread->materialCount) {

								struct Material m = rtThread->materials[materialId];

								//Add emission at every intersection (should be removed if NEE handles this)

								col = f32x4_add(col, Material_getEmissive(m));

								//TODO: Add contribution from random light using NEE
								//*Requires us to mark emissive primitives*

								//To simplify, we will pick specular and diffuse about 50/50
								//Meaning our probability is 1 / 0.5 = 2

								f32 probability = 0.5f;
								bool isSpecular = Random_sample(&seed) < 0.5f;

								contrib = f32x4_mul(contrib, f32x4_xxxx4(1 / probability));

								//Calculate real shading normal
								//This is the same as geometry normal for spheres

								f32x4 N = nrm;

								//When using waves, we should stochastically pick for this warp, instead of per pixel
								//And then multiply the entire warp by 2

								f32x4 xi2 = Random_sample2(&seed);

								if (isSpecular)
									contrib = f32x4_mul(contrib, Shading_evalSpecular(xi2, m, &r, pos, N, nrm));

								else contrib = f32x4_mul(contrib, Shading_evalDiffuse(xi2, m, &r, pos, N, nrm));

								//There is no contribution, so we should stop

								if (f32x4_w(r.originMinT) < 0)
									break;
							}

							//Invalid material reached; show it as complete black

							else break;
						}

						//We missed any geometry, this means we should hit the sky and we should terminate the ray

						else {
							col = f32x4_add(col, f32x4_mul(contrib, f32x4_srgba8Unpack(rtThread->skyColor)));
							break;
						}
					}

					accum = f32x4_add(accum, col);
				}

			accum = f32x4_div(accum, f32x4_xxxx4(superSamp2));

			u32 packed = f32x4_srgba8Pack(accum);
			Bit_appendU32(buf, packed);
		}
}

void RaytracingThread_start(
	struct RaytracingThread *rtThread,
	u16 threadOff, u16 threadCount,
	const Sphere *sph, u32 sphereCount,
	const struct Material *mat, u32 matCount,
	const u64 *materialIds,
	const struct Camera *cam, u32 skyColor,
	u16 renderWidth, u16 renderHeight, struct Buffer buf
) {
	u16 ySiz = renderHeight / threadCount;
	u16 yOff = ySiz * threadOff;

	if (threadOff == threadCount - 1)
		ySiz += renderHeight % threadCount;

	usz stride = (usz)renderWidth * 4;

	*rtThread = (struct RaytracingThread) {
		.imageBuf = Bit_subset(buf, (usz)yOff * stride, (usz)ySiz * stride),
		.cam = cam,
		.spheres = sph,
		.sphereCount = sphereCount,
		.yOffset = yOff,
		.ySize = ySiz,
		.w = renderWidth,
		.h = renderHeight,
		.skyColor = skyColor,
		.materials = mat,
		.materialCount = matCount,
		.packedMaterialIds = materialIds
	};

	rtThread->thread = Thread_start(trace, rtThread, ourAlloc, ourFree, NULL);
}

//

#define SPHERE_COUNT 4
#define MATERIAL_COUNT 2

int Program_run() {

	ns start = Timer_now();

	//Init camera, output locations and size and scene

	u16 renderWidth = 1920, renderHeight = 1080;

	quat dir = Quat_fromEuler(f32x4_init3(0, -25, 0));
	f32x4 origin = f32x4_zero();
	struct Camera cam = Camera_init(dir, origin, 45, .01f, 1000.f, renderWidth, renderHeight);

	usz renderPixels = (usz)renderWidth * renderHeight;

	u32 skyColor = 0xFF0080FF;

	const c8 *outputBmp = "output.bmp";

	//Init spheres

	Sphere sph[SPHERE_COUNT] = { 
		Sphere_init(f32x4_init3(5, -2, 0), 1), 
		Sphere_init(f32x4_init3(5, 0, -2), 1), 
		Sphere_init(f32x4_init3(5, 0, 2),  1), 
		Sphere_init(f32x4_init3(5, 2, 0),  1)
	};

	struct Material material[MATERIAL_COUNT] = {

		Material_initMetal(
			f32x4_init3(1, 0, 0), 0,		//albedo, roughness
			f32x4_xxxx4(0), 0,			//emissive, anisotropy
			0, 0, 0						//clearcoat, clearcoatRoughness, transparency
		),

		Material_initDielectric(
			f32x4_init3(0, 1, 0), 0.5,	//albedo, specular (*8%)
			f32x4_xxxx4(0), 0,			//emissive, roughness
			0, 0, 0, 0,					//clearcoat, clearcoatRoughness, sheen, sheenTint,
			0, 0, 0, 0,					//subsurface, scatter distance, transparency, translucency, 
			0, 0						//absorption multiplier, ior
		)
	};

	u64 materialIds[(SPHERE_COUNT + 2) / 3] = {
		u64_pack21x3(0, 1, 0),
		u64_pack21x3(0, 0, 0)
	};

	//Output image

	struct Buffer buf = Bit_bytes(renderPixels << 2, ourAlloc, NULL);

	//Setup threads

	u16 threadsToRun = (u16) Math_maxu(u16_MAX, Thread_getLogicalCores());

	usz threadsSize = sizeof(struct RaytracingThread) * threadsToRun;

	struct RaytracingThread *threads = ourAlloc(NULL, threadsSize);

	//Start threads

	for (u16 i = 0; i < threadsToRun; ++i)
		RaytracingThread_start(
			threads + i, i, threadsToRun,
			sph, SPHERE_COUNT, 
			material, MATERIAL_COUNT,
			materialIds,
			&cam, skyColor, 
			renderWidth, renderHeight, buf
		);

	//Clean up threads

	for (u16 i = 0; i < threadsToRun; ++i)
		Thread_waitAndCleanup(&threads[i].thread, 0);

	ourFree(NULL, (struct Buffer) { (u8*) threads, threadsSize });

	//Output to file

	struct Buffer file = BMP_writeRGBA(buf, renderWidth, renderHeight, false, ourAlloc, NULL);
	File_write(file, outputBmp);
	Bit_free(&file, ourFree, NULL);

	//Free image and tell how long it took

	Bit_free(&buf, ourFree, NULL);

	printf("Finished in %fms\n", (f32)Timer_elapsed(start) / ms);

	return 0;
}