#include "math/camera.h"
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
};

#define SUPER_SAMPLING 4
#define SUPER_SAMPLING2 (SUPER_SAMPLING * SUPER_SAMPLING)

void trace(struct RaytracingThread *rtThread) {

	struct Ray r;
	struct Intersection inter;

	const struct Camera *c = rtThread->cam;
	struct Buffer *buf = &rtThread->imageBuf;

	u16 w = rtThread->w, h = rtThread->h;

	for (u16 j = rtThread->yOffset, jMax = j + rtThread->ySize; j < jMax; ++j)
		for (u16 i = 0; i < w; ++i) {

			f32x4 accum = Vec_zero();

			for(u16 jj = 0; jj < SUPER_SAMPLING; ++jj)
				for(u16 ii = 0; ii < SUPER_SAMPLING; ++ii) {

					Camera_genRay(c, &r, i, j, w, h, ii / (f32)SUPER_SAMPLING, jj / (f32)SUPER_SAMPLING);

					Intersection_init(&inter);

					//Get intersection

					for (u32 k = 0; k < rtThread->sphereCount; ++k)
						Sphere_intersect(rtThread->spheres[k], r, &inter, k);

					//

					f32x4 col;

					if (inter.hitT >= 0) {

						//Process intersection

						f32x4 pos = Vec_add(r.originMinT, Vec_mul(r.dirMaxT, Vec_xxxx4(inter.hitT)));
						f32x4 nrm = Vec_normalize3(Vec_sub(pos, rtThread->spheres[inter.object]));

						f32x4 nrmCol = Vec_add(Vec_mul(nrm, Vec_xxxx4(.5f)), Vec_xxxx4(.5f));

						col = nrmCol;
					}

					else col = Vec_srgba8Unpack(rtThread->skyColor);

					accum = Vec_add(accum, col);
				}

			accum = Vec_div(accum, Vec_xxxx4(SUPER_SAMPLING2));

			u32 packed = Vec_srgba8Pack(accum);
			Bit_appendU32(buf, packed);
		}
}

void RaytracingThread_start(
	struct RaytracingThread *rtThread,
	u16 threadOff, u16 threadCount,
	const Sphere *sph, u32 sphereCount,
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
		.skyColor = skyColor
	};

	rtThread->thread = Thread_start(trace, rtThread, ourAlloc, ourFree, NULL);
}

//

#define SPHERE_COUNT 4

int Program_run() {

	ns start = Timer_now();

	//Init camera, output locations and size and scene

	u16 renderWidth = 1920, renderHeight = 1080;

	quat dir = Quat_fromEuler(Vec_init3(0, -25, 0));
	f32x4 origin = Vec_zero();
	struct Camera cam = Camera_init(dir, origin, 45, .01f, 1000.f, renderWidth, renderHeight);

	usz renderPixels = (usz)renderWidth * renderHeight;

	u32 skyColor = 0xFF0080FF;

	const c8 *outputBmp = "output.bmp";

	//Init spheres

	Sphere sph[SPHERE_COUNT] = { 
		Sphere_init(Vec_init3(5, -2, 0), 1), 
		Sphere_init(Vec_init3(5, 0, -2), 1), 
		Sphere_init(Vec_init3(5, 0, 2),  1), 
		Sphere_init(Vec_init3(5, 2, 0),  1)
	};

	//Output image

	struct Buffer buf = Bit_bytes(renderPixels << 2, ourAlloc, NULL);

	//Setup threads

	u16 threadsToRun = (u16)Thread_getLogicalCores();

	usz threadsSize = sizeof(struct RaytracingThread) * threadsToRun;

	struct RaytracingThread *threads = ourAlloc(NULL, threadsSize);

	//Start threads

	for (u16 i = 0; i < threadsToRun; ++i)
		RaytracingThread_start(
			threads + i, i, threadsToRun,
			sph, SPHERE_COUNT, 
			&cam, skyColor, 
			renderWidth, renderHeight, buf
		);

	//Clean up threads

	for (usz i = 0; i < threadsToRun; ++i)
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