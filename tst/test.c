#include "math/camera.h"
#include "types/buffer.h"
#include "types/timer.h"
#include "file/file.h"
#include "formats/bmp.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include "platforms/window.h"
#include "platforms/log.h"
#include <stdio.h>

//Handling multi threaded tracing

struct RaytracingThread {

	struct Thread *thread;

	struct Buffer imageBuf;
	const struct Camera *cam;

	const Sphere *spheres;

	U32 sphereCount;

	U16 yOffset, ySize;
	U16 w, h;

	U32 skyColor;
};

#define SUPER_SAMPLING 4
#define SUPER_SAMPLING2 (SUPER_SAMPLING * SUPER_SAMPLING)

void trace(struct RaytracingThread *rtThread) {

	struct Ray r;
	struct Intersection inter;

	const struct Camera *c = rtThread->cam;
	struct Buffer *buf = &rtThread->imageBuf;

	U16 w = rtThread->w, h = rtThread->h;

	for (U16 j = rtThread->yOffset, jMax = j + rtThread->ySize; j < jMax; ++j)
		for (U16 i = 0; i < w; ++i) {

			F32x4 accum = F32x4_zero();

			for(U16 jj = 0; jj < SUPER_SAMPLING; ++jj)
				for(U16 ii = 0; ii < SUPER_SAMPLING; ++ii) {

					if(!Camera_genRay(c, &r, i, j, w, h, ii / (F32)SUPER_SAMPLING, jj / (F32)SUPER_SAMPLING))
						continue;

					Intersection_create(&inter);

					//Get intersection

					for (U32 k = 0; k < rtThread->sphereCount; ++k)
						Sphere_intersect(rtThread->spheres[k], r, &inter, k);

					//

					F32x4 col;

					if (inter.hitT >= 0) {

						//Process intersection

						F32x4 pos = F32x4_add(r.originMinT, F32x4_mul(r.dirMaxT, F32x4_xxxx4(inter.hitT)));
						F32x4 nrm = F32x4_normalize3(F32x4_sub(pos, rtThread->spheres[inter.object]));

						F32x4 nrmCol = F32x4_add(F32x4_mul(nrm, F32x4_xxxx4(.5f)), F32x4_xxxx4(.5f));

						col = nrmCol;
					}

					else col = F32x4_srgba8Unpack(rtThread->skyColor);

					accum = F32x4_add(accum, col);
				}

			accum = F32x4_div(accum, F32x4_xxxx4(SUPER_SAMPLING2));

			U32 packed = F32x4_srgba8Pack(accum);

			if(Buffer_appendU32(buf, packed).genericError)
				return;
		}
}

struct Error RaytracingThread_create(
	struct RaytracingThread *rtThread,
	U16 threadOff, U16 threadCount,
	const Sphere *sph, U32 sphereCount,
	const struct Camera *cam, U32 skyColor,
	U16 renderWidth, U16 renderHeight, struct Buffer buf
) {
	U16 ySiz = renderHeight / threadCount;
	U16 yOff = ySiz * threadOff;

	if (threadOff == threadCount - 1)
		ySiz += renderHeight % threadCount;

	U64 stride = (U64)renderWidth * 4;

	*rtThread = (struct RaytracingThread) {
		.cam = cam,
		.spheres = sph,
		.sphereCount = sphereCount,
		.yOffset = yOff,
		.ySize = ySiz,
		.w = renderWidth,
		.h = renderHeight,
		.skyColor = skyColor
	};

	struct Error err = Buffer_createSubset(buf, (U64)yOff * stride, (U64)ySiz * stride, &rtThread->imageBuf);

	if(err.genericError)
		return err;

	if((err = Thread_create(trace, rtThread, &rtThread->thread)).genericError)
		return err;

	return Error_none();
}

//

static const U16 renderWidth = 1920, renderHeight = 1080;
static const U32 skyColor = 0xFF0080FF;

static enum TextureFormat format = TextureFormat_rgba8;

void Program_exit() { }

void draw(struct Window *w) {

	struct Error err;

	if((err = Window_presentCPUBuffer(w, String_createRefUnsafeConst("output.bmp"))).genericError)
		Log_error(String_createRefUnsafeConst("Couldn't present CPU buffer"), LogOptions_Default);

	//We completed our draw
	//This is not done by default, because we might want to render multiple frames

	if(Window_isVirtual(w))
		w->flags &= ~WindowFlags_IsActive;
}

int Program_run() {

	Ns start = Timer_now();

	//Init camera, output locations and size and scene

	Quat dir = Quat_fromEuler(F32x4_create3(0, -25, 0));
	struct Camera cam = Camera_create(dir, F32x4_zero(), 45, .01f, 1000.f, renderWidth, renderHeight);

	//Init spheres

	Sphere sph[] = { 
		Sphere_create(F32x4_create3(5, -2, 0), 1), 
		Sphere_create(F32x4_create3(5, 0, -2), 1), 
		Sphere_create(F32x4_create3(5, 0, 2),  1), 
		Sphere_create(F32x4_create3(5, 2, 0),  1)
	};

	U64 spheres = sizeof(sph) / sizeof(sph[0]);

	//Output image

	struct Error err;

	//Setup threads

	U16 threadsToRun = (U16)Thread_getLogicalCores();

	U64 threadsSize = sizeof(struct RaytracingThread) * threadsToRun;

	struct Buffer bufThreads = Buffer_createNull();

	if((err = Buffer_createUninitializedBytes(threadsSize, Platform_instance.alloc, &bufThreads)).genericError)
		return 2;

	struct RaytracingThread *threads = (struct RaytracingThread*) bufThreads.ptr;

	//Setup buffer

	struct Window *wind = NULL;

	if (!WindowManager_lock(&Platform_instance.windowManager, U64_MAX)) {
		Buffer_free(&bufThreads, Platform_instance.alloc);
		return 6;
	}

	struct WindowCallbacks callbacks = (struct WindowCallbacks) { 0 };
	callbacks.draw = draw;

	if((err = WindowManager_create(
		&Platform_instance.windowManager,
		I32x2_zero(), I32x2_create2(renderWidth, renderHeight), 
		WindowHint_DisableResize | WindowHint_ProvideCPUBuffer, 
		String_createRefUnsafeConst("Rt core test"),
		callbacks,
		(enum WindowFormat) format,
		&wind
	)).genericError) {
		WindowManager_unlock(&Platform_instance.windowManager);
		Buffer_free(&bufThreads, Platform_instance.alloc);
		return 4;
	}

	//Start threads

	for (U16 i = 0; i < threadsToRun; ++i) 

		if ((err = RaytracingThread_create(
			threads + i, i, threadsToRun,
			sph, spheres,
			&cam, skyColor,
			renderWidth, renderHeight, wind->cpuVisibleBuffer
		)).genericError) { 

			for(U16 j = 0; j < i; ++j)
				Thread_waitAndCleanup(&threads[j].thread, 0);

			WindowManager_unlock(&Platform_instance.windowManager);
			Buffer_free(&bufThreads, Platform_instance.alloc);
			WindowManager_free(&Platform_instance.windowManager, &wind);
			return 3;
		}

	//Clean up threads

	for (U64 i = 0; i < threadsToRun; ++i)
		Thread_waitAndCleanup(&threads[i].thread, 0);

	Buffer_free(&bufThreads, Platform_instance.alloc);

	//Finished render

	printf("Finished in %fms\n", (F32)Timer_elapsed(start) / ms);

	//Wait for user to close the window

	WindowManager_unlock(&Platform_instance.windowManager);			//We don't need to do anything now
	WindowManager_waitForExitAll(wind, U64_MAX);

	return 0;
}