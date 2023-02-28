/* OxC3(Oxsomi core 3), a general framework and toolset for cross platform applications.
*  Copyright (C) 2023 Oxsomi / Nielsbishere (Niels Brunekreef)
*  
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*  
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*  
*  You should have received a copy of the GNU General Public License
*  along with this program. If not, see https://github.com/Oxsomi/core3/blob/main/LICENSE.
*  Be aware that GPL3 requires closed source products to be GPL3 too if released to the public.
*  To prevent this a separate license will have to be requested at contact@osomi.net for a premium;
*  This is called dual licensing.
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

const Bool Platform_useWorkingDirectory = false;

#define _SUPER_SAMPLING 1
#define _SUPER_SAMPLING2 (_SUPER_SAMPLING * _SUPER_SAMPLING)

//Handling multi threaded tracing

typedef struct Scene {

	F32x4 skyColor;

	const Sphere *spheres;

	U32 sphereCount;

} Scene;

typedef struct RaytracingThread {

	Thread *thread;

	const Scene *scene;
	const Camera *camera;
	const I32x2 *viewport;

	U32 *imageBuf;

	U16 yOffset, ySize;

	U32 threadOff;

	Lock lock;

	Bool shouldTerminate;
	Bool hasWork;

} RaytracingThread;

void RaytracingThread_trace(RaytracingThread *rtThread) {

	Ray r;
	Intersection inter;

	const Scene *scene = rtThread->scene;
	const Camera *c = rtThread->camera;

	U16 w = (U16) I32x2_x(*rtThread->viewport);
	U16 h = (U16) I32x2_y(*rtThread->viewport);

	U32 *ptr = rtThread->imageBuf;

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

					for (U32 k = 0; k < scene->sphereCount; ++k)
						Sphere_intersect(scene->spheres[k], r, &inter, k);

					//

					F32x4 col;

					if (inter.hitT >= 0) {

						//Process intersection

						F32x4 pos = F32x4_add(r.originMinT, F32x4_mul(r.dirMaxT, F32x4_xxxx4(inter.hitT)));
						F32x4 nrm = F32x4_normalize3(F32x4_sub(pos, scene->spheres[inter.object]));

						F32x4 nrmCol = F32x4_add(F32x4_mul(nrm, F32x4_xxxx4(.5f)), F32x4_xxxx4(.5f));

						col = nrmCol;
					}

					else col = scene->skyColor;

					accum = F32x4_add(accum, col);
				}

			accum = F32x4_div(accum, F32x4_xxxx4(_SUPER_SAMPLING2));

			*ptr = F32x4_srgba8Pack(accum);
			++ptr;
		}
}

//A raytracing job is just waiting to do something,
//until the next frame is requested.
//This is because launching a thread is expensive.

void RaytracingThread_job(RaytracingThread *rtThread) {

	Bool terminate = !rtThread || rtThread->shouldTerminate;

	while (!terminate) {

		//Ensure nothing is working on our thread at the moment.
		//This is possible because on a resize we do get new data.

		while(!Lock_lock(&rtThread->lock, 1 * MU))
			Thread_sleep(1 * MU);

		//Only trace if we've gotten a signal that data is ready

		if(rtThread->hasWork) {
			RaytracingThread_trace(rtThread);
			rtThread->hasWork = false;
		}

		terminate = rtThread->shouldTerminate;

		Lock_unlock(&rtThread->lock);

		//Ensure we don't waste too many resources waiting for a next job.

		if(!terminate)
			Thread_sleep(1 * MU);
	}

	Lock_free(&rtThread->lock);
}

//Globals

Scene scene;

Camera camera;
Quat camDir;

I32x2 viewport;

F32x4 camOrigin;

U16 threadCount;
RaytracingThread *threads;

U64 frameId = 0;
U64 lastErrorFrame = (U64) -1;

F32 time = 0;

//

Error RaytracingThread_create(
	RaytracingThread *rtThread,
	U16 threadOff
) {
	
	*rtThread = (RaytracingThread) {
		.threadOff = threadOff,
		.scene = &scene,
		.camera = &camera,
		.viewport = &viewport
	};

	Error err = Error_none();
	_gotoIfError(clean, Lock_create(&rtThread->lock));

	_gotoIfError(clean, Thread_create(RaytracingThread_job, rtThread, &rtThread->thread));

clean:

	if (err.genericError) {
		Lock_free(&rtThread->lock);
		*rtThread = (RaytracingThread) { 0 };
	}

	return err;
}

void Program_exit() { }

void onUpdate(Window *w, F32 dt) {

	F32 prevTime = time;

	time += dt;

	if(F32_floor(prevTime) != F32_floor(time))
		Log_debugLn("%ufps", (U32)F32_round(1 / dt));

	//Setup camera

	viewport = w->size;

	U16 renderWidth = (U16) I32x2_x(w->size);
	U16 renderHeight = (U16) I32x2_y(w->size);

	camera = Camera_create(
		camDir,
		camOrigin,
		45, 
		.01f, 
		1000.f, 
		renderWidth, 
		renderHeight
	);

	//Animate sky color

	F32 perc = F32_cos(time) * 0.5f + 0.5f;
	scene.skyColor = F32x4_mul(F32x4_create4(0.25f, 0.5f, 1, 1), F32x4_xxxx4(perc));
}

void onDraw(Window *w) {

	++frameId;

	U16 renderWidth = (U16) I32x2_x(w->size);
	U16 renderHeight = (U16) I32x2_y(w->size);

	//Queue up work for the jobs

	for(U16 i = 0; i < threadCount; ++i) {

		U16 ySiz = renderHeight / threadCount;
		U16 yOff = ySiz * i;

		if (i == threadCount - 1)
			ySiz += renderHeight % threadCount;

		U64 stride = (U64)renderWidth * sizeof(U32);

		Buffer tmp = Buffer_createNull();
		Error err = Buffer_createSubset(
			w->cpuVisibleBuffer, 
			(U64)yOff * stride, 
			(U64)ySiz * stride, 
			false, 
			&tmp
		);

		if(err.genericError) {

			//Last frame also had an error, we just remember the frameId and move on.
			//Otherwise we get error spam.

			if (lastErrorFrame + 1 == frameId) {
				lastErrorFrame = frameId;
				continue;
			}

			if (lastErrorFrame != frameId)
				Error_printx(err, ELogLevel_Error, ELogOptions_Default);

			lastErrorFrame = frameId;
			continue;
		}

		RaytracingThread *rtThread = threads + i;

		//Wait until job is done reading

		while(!Lock_lock(&rtThread->lock, 1 * MU))
			Thread_sleep(1 * MU);

		//Prepare job

		rtThread->imageBuf = (U32*) tmp.ptr;

		rtThread->yOffset = yOff;
		rtThread->ySize = ySiz;
		rtThread->hasWork = true;

		//Tell job it can continue.

		Lock_unlock(&rtThread->lock);
	}

	//Wait for them to complete their job

	for (U16 i = 0; i < threadCount; ++i) {

		RaytracingThread *rtThread = threads + i;

		Bool isActive = true;

		while(isActive) {

			while(!Lock_lock(&rtThread->lock, 1 * MU))
				Thread_sleep(1 * MU);

			isActive = rtThread->hasWork;
			Lock_unlock(&rtThread->lock);

			if(isActive)
				Thread_sleep(1 * MU);
		}
	}

	//Just copy it to the result, since we just need to present it
	//We could render here if we want a dynamic scene

	Error err = Error_none();
	_gotoIfError(clean, Window_presentCPUBuffer(w, String_createConstRefUnsafe("output.bmp"), 1 * SECOND));

	//We need to signal that we're done if we're a virtual window

	goto terminate;

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);

terminate:

	if(Window_isVirtual(w))
		Window_terminate(w);
}

int Program_run() {

	//Init camera, output locations and size and scene

	camDir = Quat_fromEuler(F32x4_create3(0, -25, 0));
	camOrigin = F32x4_zero();

	//Init spheres

	Sphere sph[] = { 
		Sphere_create(F32x4_create3(5, -2, 0), 1), 
		Sphere_create(F32x4_create3(5, 0, -2), 1), 
		Sphere_create(F32x4_create3(5, 0, 2),  1), 
		Sphere_create(F32x4_create3(5, 2, 0),  1)
	};

	U32 spheres = (U32)(sizeof(sph) / sizeof(sph[0]));

	scene.sphereCount = spheres;
	scene.spheres = sph;

	//Output image

	Error err = Error_none();
	Window *wind = NULL;
	Buffer bufThreads = Buffer_createNull();
	U64 threadId = 0;

	//Setup threads

	threadCount = (U16)Thread_getLogicalCores();
	threads = NULL;

	U64 threadsSize = sizeof(RaytracingThread) * threadCount;

	_gotoIfError(clean, Buffer_createUninitializedBytesx(threadsSize, &bufThreads));

	threads = (RaytracingThread*) bufThreads.ptr;

	//Setup jobs

	for (; threadId < threadCount; ++threadId) 
		_gotoIfError(clean, RaytracingThread_create(threads + threadId, (U16) threadId));

	//Setup buffer / window

	WindowManager_lock(&Platform_instance.windowManager, U64_MAX);

	WindowCallbacks callbacks = (WindowCallbacks) { 0 };
	callbacks.onDraw = onDraw;
	callbacks.onUpdate = onUpdate;

	_gotoIfError(clean, WindowManager_createWindow(
		&Platform_instance.windowManager,
		I32x2_zero(), I32x2_create2(1920, 1080),
		EWindowHint_ProvideCPUBuffer, 
		String_createConstRefUnsafe("Rt core test"),
		callbacks,
		EWindowFormat_rgba8,
		&wind
	));

	//Wait for user to close the window

	WindowManager_unlock(&Platform_instance.windowManager);			//We don't need to do anything now
	WindowManager_waitForExitAll(&Platform_instance.windowManager, U64_MAX);

	wind = NULL;

clean:

	Error_printx(err, ELogLevel_Error, ELogOptions_Default);

	for(U16 i = 0; i < threadId; ++i) {

		RaytracingThread *rtThread = threads + i;

		//Properly tell thread to finish work

		while(!Lock_lock(&rtThread->lock, 1 * MU))
			Thread_sleep(1 * MU);

		rtThread->shouldTerminate = true;
		Lock_unlock(&rtThread->lock);

		//Wait for thread to exit

		Thread_waitAndCleanup(&rtThread->thread, U32_MAX);
	}

	if(wind && Lock_lock(&wind->lock, 5 * SECOND)) {
		Window_terminate(wind);
		Lock_unlock(&wind->lock);
	}

	WindowManager_unlock(&Platform_instance.windowManager);

	Buffer_freex(&bufThreads);
	return 1;
}
