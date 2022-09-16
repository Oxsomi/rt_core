#include "math/shading.h"
#include "math/camera.h"
//#include "math/rand.h"
#include "types/bit.h"
#include "types/timer.h"
#include "formats/bmp.h"
#include "file/file.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include "platforms/window_manager.h"
#include "platforms/window.h"
#include "types/string.h"
#include <stdio.h>
#include <stdlib.h>

//Handling multi threaded tracing

struct RaytracingThread {

	struct Thread *thread;

	struct Buffer imageBuf;
	const struct Camera *cam;

	const Sphere *spheres;

	f32x4 skyColor;

	u32 sphereCount;

	u16 yOffset, ySize;
	u16 w, h;

	const struct Material *materials;
	u32 materialCount;

	const u64 *packedMaterialIds;

	enum WindowFormat format;
};

#define SUPER_SAMPLING 1
#define MAX_BOUNCES_U8 0

void trace(struct RaytracingThread *rtThread) {

	struct Ray r;
	struct Intersection inter;

	const struct Camera *c = rtThread->cam;
	struct Buffer *buf = &rtThread->imageBuf;

	u16 w = rtThread->w, h = rtThread->h;
	//u32 frameId = 0;

	u8 superSamp = SUPER_SAMPLING, superSamp2;
	f32 invSuperSampf = 1.f / superSamp;

	if (((w * SUPER_SAMPLING) < w) || ((h * SUPER_SAMPLING) < h))
		superSamp = 1;

	superSamp2 = superSamp * superSamp;

	struct Error err = (struct Error) { 0 };

	for (u16 j = rtThread->yOffset, jMax = j + rtThread->ySize; j < jMax; ++j)
		for (u16 i = 0; i < w; ++i) {

			f32x4 accum = f32x4_zero();

			for(u16 jj = 0; jj < superSamp; ++jj)
				for(u16 ii = 0; ii < superSamp; ++ii) {

					/*u32 seed = Random_seed(
						i * superSamp + ii, j * superSamp+ jj, 
						w * superSamp,
						frameId
					);*/

					Camera_genRay(c, &r, i, j, w, h, (ii + .5f) * invSuperSampf, (jj + .5f) * invSuperSampf);

					Intersection_create(&inter);

					f32x4 contrib = f32x4_one();
					f32x4 col = f32x4_zero();

					for (u16 k = 0; k < 1 + (u16)MAX_BOUNCES_U8; ++k) {

						//Get intersection

						for (u32 l = 0; l < rtThread->sphereCount; ++l)
							Sphere_intersect(rtThread->spheres[l], r, &inter, l);

						//

						if (inter.hitT >= 0) {

							//Process intersection

							//f32x4 pos = f32x4_add(r.originMinT, f32x4_mul(r.dirMaxT, f32x4_xxxx4(inter.hitT)));
							//f32x4 nrm = f32x4_normalize3(f32x4_sub(pos, rtThread->spheres[inter.object]));

							//Grab material

							u32 materialId = u64_unpack21x3(rtThread->packedMaterialIds[inter.object / 3], inter.object % 3);

							if (materialId < rtThread->materialCount) {

								struct Material m = rtThread->materials[materialId];

								//Add emission at every intersection (should be removed if (//TODO:) NEE handles this)

								f32x4 emissive = f32x4_mul(f32x4_xxxx4(m.emissive), m.albedo);

								col = f32x4_add(col, f32x4_add(emissive, m.albedo));

								break;		//TODO: No bounces yet

								/*
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
									break;*/
							}

							//Invalid material reached; show it as complete black

							else break;
						}

						//We missed any geometry, this means we should hit the sky and we should terminate the ray

						else {
							col = f32x4_add(col, f32x4_mul(contrib, rtThread->skyColor));
							break;
						}
					}

					accum = f32x4_add(accum, col);
				}

			accum = f32x4_mul(accum, f32x4_xxxx4(1.f / superSamp2));

			bool is32Bit = true;

			switch (rtThread->format) {

				case WindowFormat_rgba32f:
					err = Bit_appendF32x4(buf, accum);
					is32Bit = false;
					break;

				case WindowFormat_rgba16f:
					err = Bit_appendI32x2(buf, f32x4_packRgba16f(accum));		//TODO: Rgba16f pack
					is32Bit = false;
					break;
			}

			if (!is32Bit) {

				if (err.genericError) {		//Quit rendering, something really bad happened
					i = w;
					j = jMax;
					break;
				}

				continue;
			}

			//TODO: HDR10 pack

			u32 packed = rtThread->format == WindowFormat_rgba8 ? f32x4_srgba8Pack(accum) : f32x4_hdr10Pack(accum);
			err = Bit_appendU32(buf, packed);

			if (err.genericError) {		//Quit rendering, something really bad happened
				i = w;
				j = jMax;
				break;
			}
		}
}

struct Error RaytracingThread_start(

	struct RaytracingThread *rtThread,

	u16 threadOff, u16 threadCount,
	const Sphere *sph, u32 sphereCount,
	const struct Material *mat, u32 matCount,
	const u64 *materialIds,
	const struct Camera *cam, f32x4 skyColor,

	struct Window *w

) {
	u16 renderWidth = (u16) i32x2_x(w->size);
	u16 renderHeight = (u16) i32x2_y(w->size);

	u16 ySiz = renderHeight / threadCount;
	u16 yOff = ySiz * threadOff;

	if (threadOff == threadCount - 1)
		ySiz += renderHeight % threadCount;

	usz stride = (usz)renderWidth * (TextureFormat_getBits((enum TextureFormat) w->format) / 8);

	*rtThread = (struct RaytracingThread) {

		.imageBuf = Bit_subset(w->cpuVisibleBuffer, (usz)yOff * stride, (usz)ySiz * stride),

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
		.packedMaterialIds = materialIds,

		.format = w->format
	};

	return Thread_create(trace, rtThread, &rtThread->thread);
}

//

#define SPHERE_COUNT 4
#define MATERIAL_COUNT 2

static const u16 renderWidth = 1920, renderHeight = 1080;
static const f32 fovDeg = 45, far = .01f, near = 1000.f;

int Program_run() {

	ns start = Timer_now();

	//Init camera, output locations and size (for aspect) and scene

	quat dir = Quat_fromEuler(f32x4_init3(0, -25, 0));
	f32x4 origin = f32x4_zero();

	f32x4 skyColor = f32x4_init4(0.25, 0.5, 1, 1);		//Sky blue

	struct Camera cam = Camera_create(dir, origin, fovDeg, far, near, renderWidth, renderHeight);

	//Init spheres

	Sphere sph[SPHERE_COUNT] = { 
		Sphere_create(f32x4_init3(5, -2, 0), 1), 
		Sphere_create(f32x4_init3(5, 0, -2), 1), 
		Sphere_create(f32x4_init3(5, 0, 2),  1), 
		Sphere_create(f32x4_init3(5, 2, 0),  1)
	};

	struct Material material[MATERIAL_COUNT] = {

		Material_createMetal(
			f32x4_init3(1, 0, 0), 0,	//albedo, roughness
			0, 0,						//emissive, anisotropy
			0, 0, 0						//clearcoat, clearcoatRoughness, transparency
		),

		Material_createDielectric(
			f32x4_init3(0, 1, 0), 0.5,	//albedo, specular (*8%)
			0, 0,						//emissive, roughness
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

	struct Window *w = NULL;
	struct Error err = (struct Error) { 0 };

	if(WindowManager_lock(&Platform_instance.windowManager, 5 * seconds)) {

		//Only use HDR if supported by the OS
		//Otherwise, we default to sRGBA8
		//If there are no more windows or we're headless, we need to use RGBA too for now
		//In the future we could support outputting this raw (after HDR file support)

		bool useHdr = 
			WindowManager_getEmptyPhysicalWindows(Platform_instance.windowManager) && 
			WindowManager_supportsFormat(Platform_instance.windowManager, WindowFormat_hdr10a2);

		err = WindowManager_create(
			&Platform_instance.windowManager,
			i32x2_zero(), i32x2_init2(renderWidth, renderHeight),
			WindowHint_DisableResize | WindowHint_ProvideCPUBuffer,
			String_createRefUnsafeConst("Test window"),
			(struct WindowCallbacks) { 0 },
			useHdr ? WindowFormat_hdr10a2 : WindowFormat_rgba8,
			&w
		);

		WindowManager_unlock(&Platform_instance.windowManager);

		if (err.genericError) {
			printf("Couldn't create virtual or physical window!\n");
			return 3;
		}
	}

	else {
		printf("Couldn't lock WindowManager!\n");
		return 2;
	}

	//Setup threads

	u16 threadsToRun = (u16) u64_min(u16_MAX, Thread_getLogicalCores());

	usz threadsSize = sizeof(struct RaytracingThread) * threadsToRun;

	struct Buffer buf = (struct Buffer) { 0 };
	err = Platform_instance.alloc.alloc(Platform_instance.alloc.alloc, threadsSize, &buf);

	if(err.genericError) {
		printf("Couldn't allocate threads!\n");
		return 2;
	}

	struct RaytracingThread *threads = buf.ptr;

	//Start threads

	struct Error err2 = (struct Error) { 0 };

	for (u16 i = 0; i < threadsToRun; ++i) {

		err2 = RaytracingThread_start(

			threads + i, i, threadsToRun,

			sph, SPHERE_COUNT,
			material, MATERIAL_COUNT,
			materialIds,
			&cam, skyColor,

			w
		);

		if (err2.genericError)
			err = err2;
	}

	//Clean up threads

	for (u16 i = 0; i < threadsToRun; ++i) {

		err2 = Thread_waitAndCleanup(&threads[i].thread, 0);

		if (err2.genericError)
			err = err2;
	}

	err2 = Platform_instance.alloc.free(Platform_instance.alloc.alloc, (struct Buffer) { (u8*) threads, threadsSize });

	if (err2.genericError)
		err = err2;

	if (err.genericError) {
		printf("Couldn't wait for threads or free!\n");
		return 6;
	}

	//Ensure our stuff ends up on screen

	err = Window_presentCPUBuffer(w, String_createRefUnsafeConst("output.bmp"));

	if (err.genericError) {
		printf("Couldn't show CPU result on screen!\n");
		return 4;
	}

	//Wait for all windows to close (can take forever if you don't close it yourself)

	err = WindowManager_waitForExitAll(&Platform_instance.windowManager, u64_MAX);

	if (err.genericError) {
		printf("Couldn't wait for all windows to exit\n");
		return 5;
	}

	printf("Finished in %fms\n", (f32)Timer_elapsed(start) / ms);
	return 0;
}
