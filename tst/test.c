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
#include "platforms/keyboard.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include "platforms/window.h"
#include "platforms/log.h"
#include "platforms/file.h"
#include "platforms/ext/errorx.h"
#include "platforms/ext/bufferx.h"
#include "platforms/ext/stringx.h"
#include "graphics/generic/instance.h"
#include "graphics/generic/device.h"
#include "graphics/generic/swapchain.h"
#include "graphics/generic/command_list.h"
#include "graphics/generic/pipeline.h"
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

						F32x4 pos = F32x4_add(r.origin, F32x4_mul(r.dir, F32x4_xxxx4(inter.hitT)));
						F32x4 nrm = F32x4_normalize4(F32x4_sub(pos, scene->spheres[inter.object].origin));

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
QuatF32 camDir;

I32x2 viewport;

F32x4 camOrigin;

U16 threadCount;
RaytracingThread *threads;

U64 frameId = 0, framesSinceLastSecond = 0;
F64 timeSinceLastSecond = 0;
U64 lastErrorFrame = (U64) -1;

F32 cameraMoveSpeed = 3;
F32 cameraSpeedUp1 = 2;
F32 cameraSpeedUp2 = 3;

F64 time = 0;

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

void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown) {

	if(device->type != EInputDeviceType_Keyboard)
		return;

	if(isDown && handle == EKey_F11)
		Window_toggleFullScreen(w);
}

void onUpdate(Window *w, F64 dt) {

	F64 prevTime = time;

	time += dt;

	if(F64_floor(prevTime) != F64_floor(time)) {

		Log_debugLn("%u fps", (U32)F64_round(framesSinceLastSecond / timeSinceLastSecond));

		framesSinceLastSecond = 0;
		timeSinceLastSecond = 0;
	}

	timeSinceLastSecond += dt;

	//Update camera
	//Grab all keyboard values

	F32x4 direction = F32x4_zero();

	Bool anyShiftDown = false;
	Bool anyCtrlDown = false;
	
	for (U64 i = 0; i < w->devices.length; ++i) {
	
		const InputDevice *dev = (const InputDevice*) w->devices.ptr + i;
	
		if (dev->type != EInputDeviceType_Keyboard)
			continue;
	
		F32 x = (F32)InputDevice_getCurrentState(*dev, EKey_D) - (F32)InputDevice_getCurrentState(*dev, EKey_A);
		F32 y = (F32)InputDevice_getCurrentState(*dev, EKey_E) - (F32)InputDevice_getCurrentState(*dev, EKey_Q);
		F32 z = (F32)InputDevice_getCurrentState(*dev, EKey_S) - (F32)InputDevice_getCurrentState(*dev, EKey_W);

		direction = F32x4_add(direction, F32x4_create3(x, y, z));

		if(InputDevice_getCurrentState(*dev, EKey_Shift))
			anyShiftDown = true;

		if(InputDevice_getCurrentState(*dev, EKey_Ctrl))
			anyCtrlDown = true;
	}

	if(F32x4_any(direction)) {

		F32 speed = cameraMoveSpeed;

		if(anyShiftDown)
			speed *= cameraSpeedUp1;

		if(anyCtrlDown)
			speed *= cameraSpeedUp2;
		
		direction = F32x4_mul(F32x4_normalize4(direction), F32x4_xxxx4((F32) F64_clamp(dt * speed, 0, 1)));
		direction = QuatF32_applyToNormal(camDir, direction);

		camOrigin = F32x4_add(camOrigin, direction);
	}

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

	F32 perc = (F32)F64_cos(time) * 0.5f + 0.5f;
	scene.skyColor = F32x4_mul(F32x4_create4(0.25f, 0.5f, 1, 1), F32x4_xxxx4(perc));
}

GraphicsInstanceRef *instance = NULL;
GraphicsDeviceRef *device = NULL;
SwapchainRef *swapchain = NULL;
CommandListRef *commandList = NULL;
List computeShaders;

void onDraw(Window *w) {

	++frameId;
	++framesSinceLastSecond;

	Error err = Error_none();

	//Queue up work for the jobs for CPU-only rendering

	if (w->flags & EWindowFlags_IsVirtual) {

		U16 renderWidth = (U16) I32x2_x(w->size);
		U16 renderHeight = (U16) I32x2_y(w->size);

		for(U16 i = 0; i < threadCount; ++i) {

			U16 ySiz = renderHeight / threadCount;
			U16 yOff = ySiz * i;

			if (i == threadCount - 1)
				ySiz += renderHeight % threadCount;

			U64 stride = (U64)renderWidth * sizeof(U32);

			Buffer tmp = Buffer_createNull();
			err = Buffer_createSubset(
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

		_gotoIfError(clean, Window_presentCPUBuffer(w, CharString_createConstRefCStr("output.bmp"), 1 * SECOND));
	}

	//We only have to submit commands

	else {

		List commandLists = (List) { 0 };
		List swapchains = (List) { 0 };

		_gotoIfError(clean, List_createConstRef((const U8*) &commandList, 1, sizeof(commandList), &commandLists));
		_gotoIfError(clean, List_createConstRef((const U8*) &swapchain, 1, sizeof(swapchain), &swapchains));

		_gotoIfError(clean, GraphicsDeviceRef_submitCommands(device, commandLists, swapchains));
	}

	//We need to signal that we're done if we're a virtual window

	goto terminate;

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);

terminate:

	if(Window_isVirtual(w))
		Window_terminate(w);
}

void onResize(Window *w) {

	//Only create swapchain if window is physical

	if(!(w->flags & EWindowFlags_IsVirtual))
		Swapchain_resize(SwapchainRef_ptr(swapchain));
}

void onCreate(Window *w) {

	if(!(w->flags & EWindowFlags_IsVirtual)) {

		SwapchainInfo swapchainInfo = (SwapchainInfo) { .window = w };

		Error err = GraphicsDeviceRef_createSwapchain(device, swapchainInfo, &swapchain);
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);

		if (!err.genericError && !commandList) {

			_gotoIfError(clean, GraphicsDeviceRef_createCommandList(device, 4 * KIBI, 128, KIBI, true, &commandList));

			//Record commands

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = swapchain,
				.load = ELoadAttachmentType_Clear,
				.color = (ClearColor) { .colorf = {  1, 0, 0, 1 } }
			};

			List colors = (List) { 0 };
			_gotoIfError(clean, List_createConstRef((const U8*) &attachmentInfo, 1, sizeof(AttachmentInfo), &colors));

			_gotoIfError(clean, CommandListRef_begin(commandList, true));
			_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ((PipelineRef**)computeShaders.ptr)[0]));
			_gotoIfError(clean, CommandListRef_dispatch2D(commandList, 1920, 1080));		//TODO: Allow specifying resource
			//_gotoIfError(clean, CommandListRef_setGraphicsPipeline(commandList, graphicsPipeline));
			//_gotoIfError(clean, CommandListRef_startRenderExt(commandList, I32x2_zero(), I32x2_zero(), colors, (List) { 0 }));
			//_gotoIfError(clean, CommandListRef_draw(commandList, (Draw) { .count = 3, .instanceCount = 1 }));
			//_gotoIfError(clean, CommandListRef_endRenderExt(commandList));
			_gotoIfError(clean, CommandListRef_end(commandList));

		clean:
			Error_printx(err, ELogLevel_Error, ELogOptions_Default);
		}
	}
}

void onDestroy(Window *w) {

	if(!(w->flags & EWindowFlags_IsVirtual))
		SwapchainRef_dec(&swapchain);
}

int Program_run() {

	//Init camera, output locations and size and scene

	camDir = QuatF32_fromEuler(F32x4_create3(-90, 0, 0));
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

	//Graphics test

	GraphicsApplicationInfo applicationInfo = (GraphicsApplicationInfo) {
		.name = CharString_createConstRefCStr("Rt core test"),
		.version = GraphicsApplicationInfo_Version(0, 2, 0)
	};

	GraphicsDeviceCapabilities requiredCapabilities = (GraphicsDeviceCapabilities) { 0 };
	GraphicsDeviceInfo deviceInfo = (GraphicsDeviceInfo) { 0 };

	Bool isVerbose = false;

	_gotoIfError(clean, GraphicsInstance_create(applicationInfo, isVerbose, &instance));

	_gotoIfError(clean, GraphicsInstance_getPreferredDevice(
		GraphicsInstanceRef_ptr(instance),
		requiredCapabilities,
		GraphicsInstance_vendorMaskAll,
		GraphicsInstance_deviceTypeAll,
		isVerbose,
		&deviceInfo
	));

	GraphicsDeviceInfo_print(&deviceInfo, true);

	_gotoIfError(clean, GraphicsDeviceRef_create(instance, &deviceInfo, isVerbose, &device));

	computeShaders = (List) { 0 };

	//Create pipelines

	Buffer testCompute = ...;		//TODO:
	List computeBinaries = (List) { 0 };
	_gotoIfError(clean, List_createConstRef(&testCompute, 1, sizeof(Buffer), &computeBinaries));
	_gotoIfError(clean, GraphicsDeviceRef_createPipelinesCompute(device, computeBinaries, &computeShaders));

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
	callbacks.onDeviceButton = onButton;
	callbacks.onResize = onResize;
	callbacks.onCreate = onCreate;
	callbacks.onDestroy = onDestroy;

	_gotoIfError(clean, WindowManager_createWindow(
		&Platform_instance.windowManager,
		I32x2_zero(), EResolution_get(EResolution_FHD),
		I32x2_zero(), I32x2_zero(),
		(WindowManager_MAX_PHYSICAL_WINDOWS == 0 ? EWindowHint_ProvideCPUBuffer : 0) | 
		EWindowHint_AllowFullscreen, 
		CharString_createConstRefCStr("Rt core test"),
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

	PipelineRef_decAll(&computeShaders);
	GraphicsDeviceRef_wait(device);
	CommandListRef_dec(&commandList);
	GraphicsDeviceRef_dec(&device);
	GraphicsInstanceRef_dec(&instance);

	Buffer_freex(&bufThreads);
	return 1;
}
