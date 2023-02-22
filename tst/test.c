/* MIT License
*   
*  Copyright (c) 2022 Oxsomi, Nielsbishere (Niels Brunekreef)
*  
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*  
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.
*  
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE. 
*/

#include "math/camera.h"
#include "types/buffer.h"
#include "types/time.h"
#include "formats/bmp.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include "platforms/window.h"
#include "platforms/log.h"
#include "platforms/file.h"
#include "platforms/ext/errorx.h"
#include "platforms/ext/bufferx.h"
#include "platforms/ext/stringx.h"
#include <stdio.h>

//Handling multi threaded tracing

typedef struct RaytracingThread {

	Thread *thread;

	Buffer imageBuf;
	const Camera *cam;

	const Sphere *spheres;

	U32 sphereCount;

	U16 yOffset, ySize;
	U16 w, h;

	U32 skyColor;

} RaytracingThread;

#define _SUPER_SAMPLING 4
#define _SUPER_SAMPLING2 (_SUPER_SAMPLING * _SUPER_SAMPLING)

void trace(RaytracingThread *rtThread) {

	Ray r;
	Intersection inter;

	const Camera *c = rtThread->cam;
	Buffer *buf = &rtThread->imageBuf;

	U16 w = rtThread->w, h = rtThread->h;

	for (U16 j = rtThread->yOffset, jMax = j + rtThread->ySize; j < jMax; ++j)
		for (U16 i = 0; i < w; ++i) {

			F32x4 accum = F32x4_zero();

			for(U16 jj = 0; jj < _SUPER_SAMPLING; ++jj)
				for(U16 ii = 0; ii < _SUPER_SAMPLING; ++ii) {

					if(!Camera_genRay(
						c, &r, i, j, w, h, 
						ii / (F32)_SUPER_SAMPLING, 
						jj / (F32)_SUPER_SAMPLING
					))
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

			accum = F32x4_div(accum, F32x4_xxxx4(_SUPER_SAMPLING2));

			U32 packed = F32x4_srgba8Pack(accum);

			if(Buffer_appendU32(buf, packed).genericError)
				return;
		}
}

Error RaytracingThread_create(
	RaytracingThread *rtThread,
	U16 threadOff,
	U16 threadCount,
	const Sphere *sph,
	U32 sphereCount,
	const Camera *cam,
	U32 skyColor,
	U16 renderWidth,
	U16 renderHeight,
	Buffer buf
) {
	U16 ySiz = renderHeight / threadCount;
	U16 yOff = ySiz * threadOff;

	if (threadOff == threadCount - 1)
		ySiz += renderHeight % threadCount;

	U64 stride = (U64)renderWidth * sizeof(U32);

	*rtThread = (RaytracingThread) {
		.cam = cam,
		.spheres = sph,
		.sphereCount = sphereCount,
		.yOffset = yOff,
		.ySize = ySiz,
		.w = renderWidth,
		.h = renderHeight,
		.skyColor = skyColor
	};

	Error err = Buffer_createSubset(buf, (U64)yOff * stride, (U64)ySiz * stride, false, &rtThread->imageBuf);

	if(err.genericError)
		return err;

	if((err = Thread_create(trace, rtThread, &rtThread->thread)).genericError)
		return err;

	return Error_none();
}

//

static const U16 RENDER_WIDTH = 1920, RENDER_HEIGHT = 1080;
static const U32 SKY_COLOR = 0xFF0080FF;

static ETextureFormat FORMAT = ETextureFormat_rgba8;

void Program_exit() { }

//Ensure we only draw after a frame has been generated

Lock ourLock;

void onDraw(Window *w) {

	//Ensure we're ready for present, since we don't draw in this thread

	if(!Lock_lock(&ourLock, U64_MAX))
		goto terminate;

	//Just copy it to the result, since we just need to present it
	//We could render here if we want a dynamic scene

	Error err = Error_none();
	_gotoIfError(err, Window_presentCPUBuffer(w, String_createConstRefUnsafe("output.bmp"), 1 * SECOND));

	//We need to signal that we're done if we're a virtual window

	goto terminate;

err:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
terminate:
	Window_terminateVirtual(w);
}

//

int Program_run() {

	Ns start = Time_now();

	//Init camera, output locations and size and scene

	Quat dir = Quat_fromEuler(F32x4_create3(0, -25, 0));
	Camera cam = Camera_create(dir, F32x4_zero(), 45, .01f, 1000.f, RENDER_WIDTH, RENDER_HEIGHT);

	//Init spheres

	Sphere sph[] = { 
		Sphere_create(F32x4_create3(5, -2, 0), 1), 
		Sphere_create(F32x4_create3(5, 0, -2), 1), 
		Sphere_create(F32x4_create3(5, 0, 2),  1), 
		Sphere_create(F32x4_create3(5, 2, 0),  1)
	};

	U32 spheres = (U32)(sizeof(sph) / sizeof(sph[0]));

	//Output image

	Error err = Error_none();
	Buffer bufThreads = Buffer_createNull();
	ourLock = (Lock) { 0 };
	Window *wind = NULL;
	U16 threadId = 0;
	RaytracingThread *threads = NULL;

	//Setup threads

	U16 threadsToRun = (U16)Thread_getLogicalCores();

	U64 threadsSize = sizeof(RaytracingThread) * threadsToRun;

	_gotoIfError(clean, Buffer_createUninitializedBytesx(threadsSize, &bufThreads));

	threads = (RaytracingThread*) bufThreads.ptr;

	//Setup lock

	_gotoIfError(clean, Lock_create(&ourLock));

	if (!Lock_lock(&ourLock, 5 * SECOND))
		_gotoIfError(clean, Error_timedOut(0, 5 * SECOND));

	//Setup buffer / window

	if (!WindowManager_lock(&Platform_instance.windowManager, U64_MAX))
		_gotoIfError(clean, Error_timedOut(0, U64_MAX));

	WindowCallbacks callbacks = (WindowCallbacks) { 0 };
	callbacks.onDraw = onDraw;

	_gotoIfError(clean, WindowManager_createVirtual(
		&Platform_instance.windowManager,
		/* I32x2_zero(), */ I32x2_create2(RENDER_WIDTH, RENDER_HEIGHT),
		//EWindowHint_DisableResize | EWindowHint_ProvideCPUBuffer, 
		//String_createConstRefUnsafe("Rt core test"),
		callbacks,
		(EWindowFormat) FORMAT,
		&wind
	));

	//Start threads

	for (; threadId < threadsToRun; ++threadId) 
		_gotoIfError(clean, RaytracingThread_create(
			threads + threadId, threadId, threadsToRun,
			sph, spheres,
			&cam, SKY_COLOR,
			RENDER_WIDTH, RENDER_HEIGHT, wind->cpuVisibleBuffer
		));

	//Clean up threads

	for (U64 i = 0; i < threadsToRun; ++i)
		Thread_waitAndCleanup(&threads[i].thread, U32_MAX);

	Buffer_freex(&bufThreads);
	
	//Signal our lock as ready for present

	Lock_unlock(&ourLock);

	//Finished render

	Log_debug(ELogOptions_Default, "Finished in %ums", (Time_elapsed(start) + MS - 1) / MS);

	//Wait for user to close the window

	WindowManager_unlock(&Platform_instance.windowManager);			//We don't need to do anything now
	WindowManager_waitForExitAll(&Platform_instance.windowManager, U64_MAX);

	Lock_free(&ourLock);

	return 0;

clean:

	Error_printx(err, ELogLevel_Error, ELogOptions_Default);

	if(threads)
		for(U16 j = 0; j < threadId; ++j)
			Thread_waitAndCleanup(&threads[j].thread, U32_MAX);

	if(wind && Lock_lock(&wind->lock, 5 * SECOND))
		WindowManager_freeWindow(&Platform_instance.windowManager, &wind);

	WindowManager_unlock(&Platform_instance.windowManager);

	Lock_unlock(&ourLock);
	Lock_free(&ourLock);
	Buffer_freex(&bufThreads);
	return 1;
}
