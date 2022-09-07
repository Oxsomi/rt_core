#include "math/camera.h"
#include "types/bit.h"
#include "types/timer.h"
#include "types/assert.h"
#include "file/file.h"
#include "formats/bmp.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include "platforms/window.h"
#include <stdio.h>

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

					Intersection_create(&inter);

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

void RaytracingThread_create(
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
		.imageBuf = Bit_createSubset(buf, (usz)yOff * stride, (usz)ySiz * stride),
		.cam = cam,
		.spheres = sph,
		.sphereCount = sphereCount,
		.yOffset = yOff,
		.ySize = ySiz,
		.w = renderWidth,
		.h = renderHeight,
		.skyColor = skyColor
	};

	rtThread->thread = Thread_create(trace, rtThread);
}

//

static const u16 renderWidth = 1920, renderHeight = 1080;
static enum TextureFormat format = TextureFormat_rgba8;

void writeBmp(struct Buffer buf, struct String outputBmp, struct Allocator alloc) {
	struct Buffer file = BMP_writeRGBA(buf, renderWidth, renderHeight, false, alloc);
	File_write(file, outputBmp);
	Bit_free(&file, alloc);
}

void Program_exit() { }

int Program_run() {

	ns start = Timer_now();

	//Init camera, output locations and size and scene

	quat dir = Quat_fromEuler(Vec_create3(0, -25, 0));
	struct Camera cam = Camera_create(dir, Vec_zero(), 45, .01f, 1000.f, renderWidth, renderHeight);

	usz renderPixels = (usz)renderWidth * renderHeight;

	u32 skyColor = 0xFF0080FF;

	//Init spheres

	Sphere sph[] = { 
		Sphere_create(Vec_create3(5, -2, 0), 1), 
		Sphere_create(Vec_create3(5, 0, -2), 1), 
		Sphere_create(Vec_create3(5, 0, 2),  1), 
		Sphere_create(Vec_create3(5, 2, 0),  1)
	};

	usz spheres = sizeof(sph) / sizeof(sph[0]);

	//Output image

	struct Buffer buf = (struct Buffer){ 0 };
	
	struct Error err = Bit_createBytes(
		renderPixels * TextureFormat_getSize(format, 1, 1), Platform_instance.alloc,
		&buf
	);

	if(err.genericError)
		return 1;

	//Setup threads

	u16 threadsToRun = (u16)Thread_getLogicalCores();

	usz threadsSize = sizeof(struct RaytracingThread) * threadsToRun;

	struct RaytracingThread *threads = ourAlloc(NULL, threadsSize);

	//Start threads

	for (u16 i = 0; i < threadsToRun; ++i)
		RaytracingThread_create(
			threads + i, i, threadsToRun,
			sph, spheres, 
			&cam, skyColor, 
			renderWidth, renderHeight, buf
		);

	//Clean up threads

	for (usz i = 0; i < threadsToRun; ++i)
		Thread_waitAndCleanup(&threads[i].thread, 0);

	ourFree(NULL, (struct Buffer) { (u8*) threads, threadsSize });

	//Finished render

	printf("Finished in %fms\n", (f32)Timer_elapsed(start) / ms);

	//Put everything onto our screen

	struct Window *wind = Window_create(
		Vec_zero(), Vec_create2(renderWidth, renderHeight), 
		WindowHint_DisableResize, String_createRefUnsafe("Rt core test"),
		(struct WindowCallbacks){ 0 },
		(enum WindowFormat) format
	);

	if(Window_isVirtual(wind))
		writeBmp(buf, String_createRefUnsafe("output.bmp"), Platform_instance.alloc);

	else Window_present(wind, buf, (enum WindowFormat) format);

	//Free image

	Bit_free(&buf, Platform_instance.alloc);

	//Wait for user to close the window

	Window_waitForExit(wind);
	Window_free(&wind, Platform_instance.alloc);

	return 0;
}