/* OxC3/RT Core(Oxsomi core 3/RT Core), a general framework for raytracing applications.
*  Copyright (C) 2023 - 2024 Oxsomi / Nielsbishere (Niels Brunekreef)
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

#include "platforms/ext/listx_impl.h"
#include "types/container/buffer.h"
#include "types/container/string.h"
#include "types/base/time.h"
#include "types/math/flp.h"
#include "formats/bmp/bmp.h"
#include "formats/dds/dds.h"
#include "formats/oiSH/sh_file.h"
#include "platforms/keyboard.h"
#include "platforms/platform.h"
#include "platforms/input_device.h"
#include "platforms/window_manager.h"
#include "platforms/window.h"
#include "platforms/log.h"
#include "platforms/file.h"
#include "platforms/ext/errorx.h"
#include "platforms/ext/bufferx.h"
#include "platforms/ext/stringx.h"
#include "platforms/ext/formatx.h"
#include "graphics/generic/instance.h"
#include "graphics/generic/device.h"
#include "graphics/generic/swapchain.h"
#include "graphics/generic/command_list.h"
#include "graphics/generic/commands.h"
#include "graphics/generic/pipeline.h"
#include "graphics/generic/device_buffer.h"
#include "graphics/generic/depth_stencil.h"
#include "graphics/generic/sampler.h"
#include "graphics/generic/device_texture.h"
#include "graphics/generic/render_texture.h"
#include "graphics/generic/blas.h"
#include "graphics/generic/tlas.h"
#include "atmos_helper.h"
#include "types/math/math.h"

//Globals

typedef struct TestWindowManager {

	F32x4 camPos;

	GraphicsInstanceRef *instance;
	GraphicsDeviceRef *device;
	CommandListRef *prepCommandList;
	CommandListRef *asCommandList;

	DeviceBufferRef *aabbs;							//temp buffer for holding aabbs for blasAABB
	DeviceBufferRef *vertexBuffers[2];
	DeviceBufferRef *indexBuffer;
	DeviceBufferRef *indirectDrawBuffer;			//sizeof(DrawCallIndexed) * 2
	DeviceBufferRef *indirectDispatchBuffer;		//sizeof(Dispatch) * 2
	DeviceBufferRef *deviceBuffer;					//Constant F32x3 for animating color
	DeviceBufferRef *viewProjMatrices;				//F32x4x4 (view, proj, viewProj)(normal, inverse)

	DeviceTextureRef *crabbage2049x, *crabbageCompressed;

	BLASRef *blas;									//If rt is on, the BLAS of a simple plane
	BLASRef *blasAABB;								//If rt is on, the BLAS of a few boxes
	TLASRef *tlas;									//If rt is on, contains the scene's AS

	PipelineRef *prepareIndirectPipeline, *indirectCompute, *inlineRaytracingTest;
	PipelineRef *graphicsTest, *graphicsDepthTest, *graphicsDepthTestMSAA;
	PipelineRef *raytracingPipelineTest;
	ListCommandListRef commandLists;
	ListSwapchainRef swapchains;

	SamplerRef *linear, *nearest, *anisotropic;

	U64 framesSinceLastSecond;
	F64 timeSinceLastSecond, time, timeSinceLastRender, realTime;

	F32 timeStep;
	Bool renderVirtual;
	Bool enableRt;
	Bool initialized;
	U8 pad[1];

	Ns lastTime;

	F64 JD;

} TestWindowManager;

//Per window data

typedef struct TestWindow {

	F64 time;
	CommandListRef *commandList;
	RefPtr *swapchain;					//Can be either SwapchainRef (non virtual) or RenderTextureRef (virtual)

	DepthStencilRef *depthStencil, *depthStencilMSAA;
	RenderTextureRef *renderTexture, *renderTextureMSAA, *renderTextureMSAATarget;

} TestWindow;

void onDraw(Window *w);
void onUpdate(Window *w, F64 dt);
void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown);
void onAxis(Window *w, InputDevice *device, InputHandle handle, F32 axis);
void onResize(Window *w);
void onCreate(Window *w);
void onDestroy(Window *w);
void onCursorMove(Window *w);
void onTypeChar(Window *w, CharString str);

WindowCallbacks TestWindow_getCallbacks() {
	WindowCallbacks callbacks = (WindowCallbacks) { 0 };
	callbacks.onDraw = onDraw;
	callbacks.onUpdate = onUpdate;
	callbacks.onTypeChar = onTypeChar;
	callbacks.onDeviceButton = onButton;
	callbacks.onDeviceAxis = onAxis;
	callbacks.onCursorMove = onCursorMove;
	callbacks.onResize = onResize;
	callbacks.onCreate = onCreate;
	callbacks.onDestroy = onDestroy;
	return callbacks;
}

//Functions

static const F32 timeStep = 1;

void onCursorMove(Window *w) {
	(void) w;
	//Log_debugLnx("Cursor move to %i,%i", I32x2_x(w->cursor), I32x2_y(w->cursor));
}

static Bool isVisible = false;

void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown) {

	TestWindowManager *twm = (TestWindowManager*)w->owner->extendedData.ptr;

	if(device->type != EInputDeviceType_Keyboard) {

		Log_debugLnx(
			"Button %s: %s",
			isDown ? "press" : "release",
			InputDevice_getButton(*device, InputDevice_getLocalHandle(*device, handle))->name
		);

		return;
	}

	CharString str = Keyboard_remap(device, (EKey) handle);
	Log_debugLnx("Key %s: %s", isDown ? "press" : "release", str.ptr);
	CharString_freex(&str);

	if(isDown) {

		switch ((EKey) handle) {

			case EKey_F2:
				isVisible = !isVisible;
				Platform_setKeyboardVisible(isVisible);
				break;

			//F9 we pause

			case EKey_F9: {
				F32 *ts = &twm->timeStep;
				*ts = *ts == 0 ? timeStep : 0;
				break;
			}

			//F11 we toggle full screen

			case EKey_F11:
				Window_toggleFullScreen(w, NULL);
				break;

			//F10 we spawn more windows (but only if multiple physical windows are supported, such as on desktop)

			case EKey_F10: {

				Window *wind = NULL;
				WindowManager_createWindow(
					w->owner, EWindowType_Physical,
					I32x2_zero(), EResolution_get(EResolution_FHD),
					I32x2_zero(), I32x2_zero(),
					EWindowHint_Default,
					CharString_createRefCStrConst("Rt core test (duped)"),
					TestWindow_getCallbacks(),
					EWindowFormat_AutoRGBA8,
					sizeof(TestWindow),
					&wind,
					NULL
				);

				break;
			}

			default:
				break;
		}
	}
}

void onAxis(Window *w, InputDevice *device, InputHandle handle, F32 axis) {
	(void) w; (void) device; (void) handle; (void) axis;
	/*Log_debugLnx(
		"Axis update: %s to %f",
		InputDevice_getAxis(*device, InputDevice_getLocalHandle(*device, handle))->name,
		axis
	);*/
}

void onTypeChar(Window *w, CharString str) {
	(void) w;
	Log_debugLnx("%s", str.ptr);
}

F32 targetFps = 60;		//Only if virtual window (indicates timeStep)

void onUpdate(Window *w, F64 dt) {

	F64 *time = &((TestWindow*)w->extendedData.ptr)->time;

	if(w->type == EWindowType_Physical)
		*time += dt;

	else *time += 1 / targetFps;

	//Toggle every 15s

	/*F64 mod = 0;
	F64_mod(*time, 15, &mod);

	F64 mod2 = 0;
	F64_mod(*time - dt, 15, &mod2);

	if(mod2 > mod)
		Window_toggleFullScreen(w, NULL);*/

	//Check for keys

	F32x4 delta = F32x4_zero();
	Bool anyShiftDown = false;

	for (U64 i = 0; i < w->devices.length; ++i) {

		const InputDevice id = w->devices.ptr[i];

		if(id.type != EInputDeviceType_Keyboard)
			continue;

		I8 x = (I8)InputDevice_isDown(id, EKey_D) - InputDevice_isDown(id, EKey_A);
		I8 y = (I8)InputDevice_isDown(id, EKey_E) - InputDevice_isDown(id, EKey_Q);
		I8 z = (I8)InputDevice_isDown(id, EKey_S) - InputDevice_isDown(id, EKey_W);

		x += (I8)InputDevice_isDown(id, EKey_Right) - InputDevice_isDown(id, EKey_Left);
		y += (I8)InputDevice_isDown(id, EKey_Numpad0) - InputDevice_isDown(id, EKey_RCtrl);
		z += (I8)InputDevice_isDown(id, EKey_Down) - InputDevice_isDown(id, EKey_Up);

		if(InputDevice_isDown(id, EKey_LShift) || InputDevice_isDown(id, EKey_RShift))
			anyShiftDown = true;

		delta = F32x4_add(delta, F32x4_create3(x, y, z));
	}

	if(!F32x4_any(delta))
		return;

	delta = F32x4_normalize3(delta);

	F32 mul = (F32)dt;

	if(anyShiftDown)
		mul *= 10;

	TestWindowManager *twm = (TestWindowManager*)w->owner->extendedData.ptr;

	twm->camPos = F32x4_add(twm->camPos, F32x4_mul(delta, F32x4_xxxx4(mul)));
}

void onManagerUpdate(WindowManager *windowManager, F64 dt) {

	TestWindowManager *tw = (TestWindowManager*) windowManager->extendedData.ptr;

	const F64 prevTime = tw->realTime;
	tw->time += (tw->renderVirtual ? 1 / targetFps : dt) * tw->timeStep;		//Time for rendering
	tw->realTime += dt;

	if(F64_floor(prevTime) != F64_floor(tw->realTime)) {

		Log_debugLnx("%"PRIu32" fps", (U32)F64_round(tw->framesSinceLastSecond / tw->timeSinceLastSecond));

		tw->framesSinceLastSecond = 0;
		tw->timeSinceLastSecond = 0;
	}

	tw->timeSinceLastSecond += dt;

	if(tw->lastTime)
		tw->lastTime += (Ns)(dt * SECOND * tw->timeStep);

	tw->JD = AtmosHelper_getJulianDate(tw->lastTime);
}

void onDraw(Window *w) { (void)w; }

void onManagerDraw(WindowManager *windowManager) {
	
	TestWindowManager *twm = (TestWindowManager*) windowManager->extendedData.ptr;
	++twm->framesSinceLastSecond;

	Error err = Error_none(), *e_rr = &err;
	Bool s_uccess = true;

	gotoIfError2(clean, ListCommandListRef_clear(&twm->commandLists))
	gotoIfError2(clean, ListSwapchainRef_clear(&twm->swapchains))
	gotoIfError2(clean, ListCommandListRef_reservex(&twm->commandLists, windowManager->windows.length + 1))
	gotoIfError2(clean, ListSwapchainRef_reservex(&twm->swapchains, windowManager->windows.length))

	if(!twm->initialized) {
		gotoIfError2(clean, ListCommandListRef_pushBackx(&twm->commandLists, twm->asCommandList))
		twm->initialized = true;
	}

	gotoIfError2(clean, ListCommandListRef_pushBackx(&twm->commandLists, twm->prepCommandList))

	RenderTextureRef *renderTex = NULL;
	U32 orientation = 0;

	for(U64 handle = 0; handle < windowManager->windows.length; ++handle) {

		Window *w = windowManager->windows.ptr[handle];
		Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));

		if (hasSwapchain) {

			TestWindow *tw = (TestWindow*) w->extendedData.ptr;
			CommandListRef *cmd = tw->commandList;
			RefPtr *swap = tw->swapchain;

			if(!renderTex)
				renderTex = tw->renderTexture;

			gotoIfError2(clean, ListCommandListRef_pushBackx(&twm->commandLists, cmd))

			if (swap->typeId == (ETypeId) EGraphicsTypeId_Swapchain) {
				gotoIfError2(clean, ListSwapchainRef_pushBackx(&twm->swapchains, swap))
				orientation = SwapchainRef_ptr(swap)->orientation;
			}
		}
	}

	if(twm->commandLists.length == 1)		//No windows to update, only root command list (not important without viewports)
		return;

	DeviceBuffer *deviceBuf = DeviceBufferRef_ptr(twm->deviceBuffer);

	typedef struct RuntimeData {

		U32 constantColorRead, constantColorWrite;
		U32 indirectDrawWrite, indirectDispatchWrite;

		U32 viewProjMatricesWrite, viewProjMatricesRead;
		U32 crabbage2049x, crabbageCompressed;

		U32 sampler;
		U32 tlasExt;
		U32 renderTargetWrite;
		U32 orientation;

		F32 skyDir[3];
		U32 padding1;

		F32 camPos[3];
		U32 padding2;

	} RuntimeData;

	DeviceBuffer *viewProjMatrices = DeviceBufferRef_ptr(twm->viewProjMatrices);

	F32x2 amsterdam = F32x2_create2(4.897070f, 52.377956f);
	F32x4 skyDir = F32x4_negate(AtmosHelper_getSunDir(twm->JD, amsterdam));

	F32x4 camPos = twm->camPos;

	RuntimeData data = (RuntimeData) {

		.constantColorRead = deviceBuf->readHandle,
		.constantColorWrite = deviceBuf->writeHandle,
		.indirectDrawWrite = DeviceBufferRef_ptr(twm->indirectDrawBuffer)->writeHandle,
		.indirectDispatchWrite = DeviceBufferRef_ptr(twm->indirectDispatchBuffer)->writeHandle,

		.viewProjMatricesWrite = viewProjMatrices->writeHandle,
		.viewProjMatricesRead = viewProjMatrices->readHandle,
		.crabbage2049x = TextureRef_getCurrReadHandle(twm->crabbage2049x, 0),
		.crabbageCompressed = TextureRef_getCurrReadHandle(twm->crabbageCompressed, 0),

		.sampler = SamplerRef_ptr(twm->anisotropic)->samplerLocation,
		.renderTargetWrite = TextureRef_getCurrWriteHandle(renderTex, 0),
		.orientation = orientation,

		.skyDir = { F32x4_x(skyDir), F32x4_y(skyDir), F32x4_z(skyDir) },
		.camPos = { F32x4_x(camPos), F32x4_y(camPos), F32x4_z(camPos) }
	};

	if (twm->tlas)
		data.tlasExt = TLASRef_ptr(twm->tlas)->handle;

	if(GraphicsDeviceRef_ptr(twm->device)->submitId < 8)
		Log_debugLnx("Logging first 8 frames: %"PRIu64, GraphicsDeviceRef_ptr(twm->device)->submitId);

	Buffer runtimeData = Buffer_createRefConst((const U32*)&data, sizeof(data));
	gotoIfError2(clean, GraphicsDeviceRef_submitCommands(
		twm->device, twm->commandLists, twm->swapchains, runtimeData,
		(F32)(twm->time - twm->timeSinceLastRender), (F32)twm->time
	))

	twm->timeSinceLastRender = twm->time;

clean:
	if(!s_uccess)
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onResize(Window *w) {
	
	TestWindowManager *twm = (TestWindowManager*) w->owner->extendedData.ptr;
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	CommandListRef *commandList = tw->commandList;

	Error err = Error_none(), *e_rr = &err;
	Bool s_uccess = true;
	(void) s_uccess;

	Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));
	Bool hadSwapchain = !!tw->swapchain;

	if(w->type != EWindowType_Virtual) {
		
		if(!tw->swapchain) {				//Init swapchain, we need to wait til resize to ensure everything is valid
			SwapchainInfo swapchainInfo = (SwapchainInfo) { .window = w };
			gotoIfError2(clean, GraphicsDeviceRef_createSwapchain(twm->device, swapchainInfo, false, NULL, &tw->swapchain))
		}

		else gotoIfError2(clean, GraphicsDeviceRef_wait(twm->device))
	}

	ETextureFormatId format = w->format == EWindowFormat_RGBA8 ? ETextureFormatId_RGBA8 : ETextureFormatId_BGRA8;
	
	if(!hasSwapchain) {

		gotoIfError2(cleanTemp, CommandListRef_begin(commandList, true, U64_MAX))
		gotoIfError2(cleanTemp, CommandListRef_end(commandList))

	cleanTemp:
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
		return;
	}

	if(w->type != EWindowType_Virtual && hadSwapchain)
		gotoIfError2(clean, SwapchainRef_resize(tw->swapchain))

	//Check if we need to resize our textures.
	//It's possible we don't, in case our device is rotated for example, we might still receive onResize.
	//But in that case, only the swapchain has to be recreated, not the render textures.

	U16 width = (U16) I32x2_x(w->size);
	U16 height = (U16) I32x2_y(w->size);

	Bool recreate = !tw->depthStencil;

	if(tw->depthStencil) {
		DepthStencil *ds = DepthStencilRef_ptr(tw->depthStencil);
		recreate = ds->width != width || ds->height != height;
		Log_debugLnx("Recreate: %"PRIu16"x%"PRIu16" vs %"PRIu16"x%"PRIu16, ds->width, ds->height, width, height);
	}

	if(!recreate)		//Skip everything, including re-recording commands
		goto generateCommands;

	//Resize depth stencil and render textures

	if(tw->depthStencil)
		DepthStencilRef_dec(&tw->depthStencil);

	gotoIfError2(clean, GraphicsDeviceRef_createDepthStencil(
		twm->device,
		width, height, EDepthStencilFormat_D16, false,
		EMSAASamples_Off,
		NULL,
		CharString_createRefCStrConst("Test depth stencil"),
		&tw->depthStencil
	))
	
	if(tw->renderTexture)
		RefPtr_dec(&tw->renderTexture);

	gotoIfError2(clean, GraphicsDeviceRef_createRenderTexture(
		twm->device,
		ETextureType_2D, width, height, 1, format, EGraphicsResourceFlag_ShaderRWBindless,
		EMSAASamples_Off,
		NULL,
		CharString_createRefCStrConst("Render texture"),
		&tw->renderTexture
	))

	//Resize MSAA targets

	if(!tw->depthStencilMSAA)
		gotoIfError2(clean, GraphicsDeviceRef_createDepthStencil(
			twm->device,
			256, 256, EDepthStencilFormat_D16, false,
			EMSAASamples_x4,
			NULL,
			CharString_createRefCStrConst("Test depth stencil MSAA 256x"),
			&tw->depthStencilMSAA
		))
	
	if(!tw->renderTextureMSAA)
		gotoIfError2(clean, GraphicsDeviceRef_createRenderTexture(
			twm->device,
			ETextureType_2D, 256, 256, 1, format, EGraphicsResourceFlag_None,
			EMSAASamples_x4,
			NULL,
			CharString_createRefCStrConst("Render texture MSAA"),
			&tw->renderTextureMSAA
		))
	
	if(!tw->renderTextureMSAATarget)
		gotoIfError2(clean, GraphicsDeviceRef_createRenderTexture(
			twm->device,
			ETextureType_2D, 256, 256, 1, format, EGraphicsResourceFlag_None,
			EMSAASamples_Off,
			NULL,
			CharString_createRefCStrConst("Render texture MSAA Target"),
			&tw->renderTextureMSAATarget
		))

	//Record commands
	
generateCommands:

	gotoIfError2(clean, CommandListRef_begin(commandList, true, U64_MAX))

	if(hasSwapchain) {

		enum EScopes {
			EScopes_ClearTarget,
			EScopes_RaytracingTest,
			EScopes_RaytracingPipelineTest,
			EScopes_GraphicsTest,
			EScopes_GraphicsTestMSAA,
			EScopes_Copy,
			EScopes_Copy2,
			EScopes_Clear,
			EScopes_Copy3
		};

		CharString names[] = {
			CharString_createRefCStrConst("ClearTarget"),
			CharString_createRefCStrConst("RaytracingTest"),
			CharString_createRefCStrConst("RaytracingPipelineTest"),
			CharString_createRefCStrConst("GraphicsTest"),
			CharString_createRefCStrConst("Copy"),
			CharString_createRefCStrConst("GraphicsTestMSAA"),
			CharString_createRefCStrConst("Copy2"),
			CharString_createRefCStrConst("Clear"),
			CharString_createRefCStrConst("Copy3")
		};

		Transition transitions[5] = { 0 };
		CommandScopeDependency deps[3] = { 0 };

		ListTransition transitionArr = (ListTransition) { 0 };
		ListCommandScopeDependency depsArr = (ListCommandScopeDependency) { 0 };
		gotoIfError2(clean, ListTransition_createRefConst(transitions, 5, &transitionArr))
		gotoIfError2(clean, ListCommandScopeDependency_createRefConst(deps, 3, &depsArr))

		//Raytracing overwrites render target, so we need to clear it first

		Bool hasRaytracing = twm->enableRt;

		if (hasRaytracing)
			if(!CommandListRef_startScope(
				commandList, (ListTransition) { 0 }, EScopes_ClearTarget, (ListCommandScopeDependency) { 0 }
			).genericError) {

				gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(1, 0, 0, 1), names[0]))

				gotoIfError2(clean, CommandListRef_clearImagef(
					commandList, F32x4_zero(), (ImageRange) { 0 }, tw->renderTexture
				))

				gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
				gotoIfError2(clean, CommandListRef_endScope(commandList))
			}

		//Test raytracing

		Bool disable = hasRaytracing;

		if(disable && hasRaytracing) {

			//Write using inline RT

			transitions[0] = (Transition) {
				.resource = twm->tlas,
				.stage = EPipelineStage_Compute
			};

			transitions[1] = (Transition) {
				.resource = twm->viewProjMatrices,
				.stage = EPipelineStage_Compute
			};

			transitions[2] = (Transition) {
				.resource = tw->renderTexture,
				.stage = EPipelineStage_Compute,
				.isWrite = true
			};

			deps[0] = (CommandScopeDependency) { .id = EScopes_ClearTarget };
			depsArr.length = 1;
			transitionArr.length = 3;

			if(!CommandListRef_startScope(commandList, transitionArr, EScopes_RaytracingTest, depsArr).genericError) {
				gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(1, 0, 0, 1), names[1]))
				gotoIfError2(clean, CommandListRef_setComputePipeline(commandList, twm->inlineRaytracingTest))
				gotoIfError2(clean, CommandListRef_dispatch2D(commandList, (width + 15) >> 4, (height + 7) >> 3))
				gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
				gotoIfError2(clean, CommandListRef_endScope(commandList))
			}

			//Write using raytracing pipelines

			transitions[0] = (Transition) {
				.resource = twm->tlas,
				.stage = EPipelineStage_RtStart
			};

			transitions[1] = (Transition) {
				.resource = twm->viewProjMatrices,
				.stage = EPipelineStage_RtStart
			};

			transitions[2] = (Transition) {
				.resource = tw->renderTexture,
				.stage = EPipelineStage_RtStart,
				.isWrite = true
			};

			deps[0] = (CommandScopeDependency) { .id = EScopes_RaytracingTest };
			depsArr.length = 1;
			transitionArr.length = 3;

			if(!CommandListRef_startScope(commandList, transitionArr, EScopes_RaytracingPipelineTest, depsArr).genericError) {
				gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(0, 1, 0, 1), names[2]))
				gotoIfError2(clean, CommandListRef_setRaytracingPipeline(commandList, twm->raytracingPipelineTest))
				gotoIfError2(clean, CommandListRef_dispatch2DRaysExt(commandList, 0, width, height))
				gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
				gotoIfError2(clean, CommandListRef_endScope(commandList))
			}
		}

		//Test graphics pipeline

		transitions[0] = (Transition) {
			.resource = twm->deviceBuffer,
			.stage = EPipelineStage_Pixel
		};

		transitions[1] = (Transition) {
			.resource = twm->viewProjMatrices,
			.stage = EPipelineStage_Vertex
		};

		transitions[2] = (Transition) {
			.resource = twm->crabbageCompressed,
			.stage = EPipelineStage_Pixel
		};

		transitions[3] = (Transition) {
			.resource = twm->crabbage2049x,
			.stage = EPipelineStage_Pixel
		};

		transitions[4] = (Transition) { .resource = twm->anisotropic };		//Keep sampler alive

		deps[0] = (CommandScopeDependency) { .id = EScopes_RaytracingTest };
		deps[1] = (CommandScopeDependency) { .id = EScopes_RaytracingPipelineTest };
		depsArr.length = 2;
		transitionArr.length = 5;

		if(disable && !CommandListRef_startScope(commandList, transitionArr, EScopes_GraphicsTest, depsArr).genericError) {

			gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(0, 0, 1, 1), names[3]))

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = tw->renderTexture,
				.unusedAfterRender = false,
				.load = hasRaytracing ? ELoadAttachmentType_Preserve : ELoadAttachmentType_Clear,
				.color = { .colorf = { 0.25f, 0.5f, 1, 1 } }
			};

			DepthStencilAttachmentInfo depthStencil = (DepthStencilAttachmentInfo) {
				.image = tw->depthStencil,
				.depthUnusedAfterRender = true,
				.depthLoad = ELoadAttachmentType_Clear,
				.clearDepth = 0
			};

			ListAttachmentInfo colors = (ListAttachmentInfo) { 0 };
			gotoIfError2(clean, ListAttachmentInfo_createRefConst(&attachmentInfo, 1, &colors))

			//Start render

			gotoIfError2(clean, CommandListRef_startRenderExt(
				commandList, I32x2_zero(), I32x2_zero(), colors, depthStencil
			))

			gotoIfError2(clean, CommandListRef_setViewportAndScissor(commandList, I32x2_zero(), I32x2_zero()))

			//Draw without depth

			gotoIfError2(clean, CommandListRef_setGraphicsPipeline(commandList, twm->graphicsTest))

			SetPrimitiveBuffersCmd primitiveBuffers = (SetPrimitiveBuffersCmd) {
				.vertexBuffers = { twm->vertexBuffers[0], twm->vertexBuffers[1] },
				.indexBuffer = twm->indexBuffer,
				.isIndex32Bit = false
			};

			gotoIfError2(clean, CommandListRef_setPrimitiveBuffers(commandList, primitiveBuffers))
			gotoIfError2(clean, CommandListRef_drawIndexed(commandList, 6, 1))
			gotoIfError2(clean, CommandListRef_drawIndirect(commandList, twm->indirectDrawBuffer, 0, 2, true))

			//Draw with depth

			gotoIfError2(clean, CommandListRef_setGraphicsPipeline(commandList, twm->graphicsDepthTest))

			gotoIfError2(clean, CommandListRef_drawUnindexed(commandList, 36, 64))		//Draw cubes

			gotoIfError2(clean, CommandListRef_endRenderExt(commandList))
			gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}

		//Test graphics pipeline MSAA

		transitions[0] = (Transition) {
			.resource = twm->deviceBuffer,
			.stage = EPipelineStage_Pixel
		};

		transitions[1] = (Transition) {
			.resource = twm->viewProjMatrices,
			.stage = EPipelineStage_Vertex
		};

		transitions[2] = (Transition) {
			.resource = twm->crabbageCompressed,
			.stage = EPipelineStage_Pixel
		};

		transitions[3] = (Transition) {
			.resource = twm->crabbage2049x,
			.stage = EPipelineStage_Pixel
		};

		transitions[4] = (Transition) { .resource = twm->anisotropic };		//Keep sampler alive

		deps[0] = (CommandScopeDependency) { .id = EScopes_RaytracingTest };
		deps[1] = (CommandScopeDependency) { .id = EScopes_RaytracingPipelineTest };
		depsArr.length = 2;
		transitionArr.length = 5;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_GraphicsTestMSAA, depsArr).genericError) {

			gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(1, 1, 1, 1), names[5]))

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = tw->renderTextureMSAA,
				.unusedAfterRender = true,
				.resolveMode = EMSAAResolveMode_Average,
				.load = ELoadAttachmentType_Clear,
				.resolveImage = tw->renderTextureMSAATarget,
				.color = { .colorf = { 0.25f, 0.5f, 1, 1 } }
			};

			DepthStencilAttachmentInfo depthStencil = (DepthStencilAttachmentInfo) {
				.image = tw->depthStencilMSAA,
				.depthUnusedAfterRender = true,
				.depthLoad = ELoadAttachmentType_Clear,
				.clearDepth = 0
			};

			ListAttachmentInfo colors = (ListAttachmentInfo) { 0 };
			gotoIfError2(clean, ListAttachmentInfo_createRefConst(&attachmentInfo, 1, &colors))

			//Start render

			gotoIfError2(clean, CommandListRef_startRenderExt(
				commandList, I32x2_zero(), I32x2_zero(), colors, depthStencil
			))

			gotoIfError2(clean, CommandListRef_setViewportAndScissor(commandList, I32x2_zero(), I32x2_zero()))

			//Draw with depth

			gotoIfError2(clean, CommandListRef_setGraphicsPipeline(commandList, twm->graphicsDepthTestMSAA))

			gotoIfError2(clean, CommandListRef_drawUnindexed(commandList, 36, 64))		//Draw cubes

			gotoIfError2(clean, CommandListRef_endRenderExt(commandList))
			gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}

		//Copy

		deps[0] = (CommandScopeDependency) { .id = EScopes_RaytracingTest };
		deps[1] = (CommandScopeDependency) { .id = EScopes_RaytracingPipelineTest };
		depsArr.length = 2;
		transitionArr.length = 0;

		if(disable && !CommandListRef_startScope(commandList, transitionArr, EScopes_Copy, depsArr).genericError) {

			gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(1, 0, 1, 1), names[4]))

			gotoIfError2(clean, CommandListRef_copyImage(
				commandList, tw->renderTexture, tw->swapchain, (CopyImageRegion) { 0 }
			))

			gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}

		//Copy2 (needs separate scope to handle write hazard)

		deps[0] = (CommandScopeDependency) { .id = EScopes_GraphicsTestMSAA };
		deps[1] = (CommandScopeDependency) { .id = EScopes_Copy };
		depsArr.length = 2;
		transitionArr.length = 0;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_Copy2, depsArr).genericError) {

			gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(1, 1, 1, 1), names[6]))

			gotoIfError2(clean, CommandListRef_copyImage(
				commandList, tw->renderTextureMSAATarget, tw->swapchain, (CopyImageRegion) { .outputRotation = w->orientation / 90 }
			))

			gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}

		//Clear target

		deps[0] = (CommandScopeDependency) { .id = EScopes_Copy2 };
		depsArr.length = 1;
		transitionArr.length = 0;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_Clear, depsArr).genericError) {

			gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(0, 0, 0, 1), names[7]))

			gotoIfError2(clean, CommandListRef_clearImagef(
				commandList, F32x4_create4(0, 1, 0, 1), (ImageRange) { 0 }, tw->renderTextureMSAATarget
			))

			gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}

		//Copy3 (needs separate scope to handle write hazard)

		UnifiedTexture tex = TextureRef_getUnifiedTexture(tw->renderTextureMSAATarget, NULL);

		if(I32x2_all(I32x2_leq(I32x2_add(I32x2_create2(tex.width, tex.height), I32x2_xx2(256)), w->size))) {

			deps[0] = (CommandScopeDependency) { .id = EScopes_Clear };
			depsArr.length = 1;
			transitionArr.length = 0;

			if(!CommandListRef_startScope(commandList, transitionArr, EScopes_Copy3, depsArr).genericError) {

				gotoIfError2(clean, CommandListRef_startRegionDebugExt(commandList, F32x4_create4(0.5, 0.5, 0.5, 1), names[8]))
			
				gotoIfError2(clean, CommandListRef_copyImage(
					commandList, tw->renderTextureMSAATarget, tw->swapchain,
					(CopyImageRegion) { .dstX = 256, .dstY = 256, .outputRotation = w->orientation / 90 }
				))

				gotoIfError2(clean, CommandListRef_endRegionDebugExt(commandList))
				gotoIfError2(clean, CommandListRef_endScope(commandList))
			}
		}
	}

	gotoIfError2(clean, CommandListRef_end(commandList))
	
clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onCreate(Window *w) {

	TestWindowManager *twm = (TestWindowManager*) w->owner->extendedData.ptr;
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;

	Error err = Error_none(), *e_rr = &err;
	Bool s_uccess = true;

	gotoIfError2(clean, GraphicsDeviceRef_createCommandList(twm->device, 2 * KIBI, 64, 64, true, &tw->commandList))

clean:
	if(!s_uccess)
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onDestroy(Window *w) {
	Log_debugLnx("On destroy");
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	RefPtr_dec(&tw->swapchain);
	DepthStencilRef_dec(&tw->depthStencil);
	RenderTextureRef_dec(&tw->renderTexture);
	DepthStencilRef_dec(&tw->depthStencilMSAA);
	RenderTextureRef_dec(&tw->renderTextureMSAA);
	RenderTextureRef_dec(&tw->renderTextureMSAATarget);
	CommandListRef_dec(&tw->commandList);
	Log_debugLnx("On destroy finished");
}

typedef struct VertexPosBuffer {
	F16 pos[2];
} VertexPosBuffer;

typedef struct VertexDataBuffer {
	F16 uv[2];
} VertexDataBuffer;

Bool renderVirtual = false;		//Whether there's a physical swapchain

void onManagerCreate(WindowManager *manager) {
	
	Error err = Error_none(), *e_rr = &err;
	Bool s_uccess = true;

	Buffer tempBuffers[4] = { 0 };
	SHFile tmpBinaries[2] = { 0 };
	ListSubResourceData subResource = (ListSubResourceData) { 0 };

	TestWindowManager *twm = (TestWindowManager*) manager->extendedData.ptr;
	twm->timeStep = timeStep;
	twm->renderVirtual = renderVirtual;
	twm->lastTime = Time_now();
	twm->JD = AtmosHelper_getJulianDate(twm->lastTime);

	//Graphics test

	Log_debugLnx("Create instance");

	GraphicsApplicationInfo applicationInfo = (GraphicsApplicationInfo) {
		.name = CharString_createRefCStrConst("Rt core test"),
		.version = OXC3_MAKE_VERSION(OXC3_MAJOR, OXC3_MINOR, OXC3_PATCH)
	};

	GraphicsDeviceCapabilities requiredCapabilities = (GraphicsDeviceCapabilities) { 0 };
	GraphicsDeviceInfo deviceInfo = (GraphicsDeviceInfo) { 0 };
	
	gotoIfError3(clean, GraphicsInterface_create(e_rr))
	
	gotoIfError2(clean, GraphicsInstance_create(
		applicationInfo,
		EGraphicsApi_Direct3D12,
		EGraphicsInstanceFlags_None,
		&twm->instance
	))
	
	gotoIfError2(clean, GraphicsInstance_getPreferredDevice(
		GraphicsInstanceRef_ptr(twm->instance),
		requiredCapabilities,
		GraphicsInstance_vendorMaskAll,
		//1 << EGraphicsDeviceType_Integrated,
		//1 << EGraphicsDeviceType_CPU,
		GraphicsInstance_deviceTypeAll,
		&deviceInfo
	))
	
	GraphicsDeviceInfo_print(GraphicsInstanceRef_ptr(twm->instance)->api, &deviceInfo, true);

	Log_debugLnx("Create device");

	gotoIfError2(clean, GraphicsDeviceRef_create(
		twm->instance,
		&deviceInfo,
		EGraphicsDeviceFlags_None,
		EGraphicsBufferingMode_Default,
		&twm->device
	))

	twm->enableRt = !!(deviceInfo.capabilities.features & (EGraphicsFeatures_RayQuery | EGraphicsFeatures_RayPipeline));

	//Create samplers

	Log_debugLnx("Create samplers");

	CharString samplerNames[] = {
		CharString_createRefCStrConst("Nearest sampler"),
		CharString_createRefCStrConst("Linear sampler"),
		CharString_createRefCStrConst("Anistropic sampler")
	};

	SamplerInfo nearestSampler = (SamplerInfo) { .filter = ESamplerFilterMode_Nearest };
	SamplerInfo linearSampler = (SamplerInfo) { .filter = ESamplerFilterMode_Linear };
	SamplerInfo anisotropicSampler = (SamplerInfo) { .filter = ESamplerFilterMode_Linear, .aniso = 16 };

	gotoIfError2(clean, GraphicsDeviceRef_createSampler(twm->device, nearestSampler, false, NULL, samplerNames[0], &twm->nearest))
	gotoIfError2(clean, GraphicsDeviceRef_createSampler(twm->device, linearSampler, false, NULL, samplerNames[1], &twm->linear))
	gotoIfError2(clean, GraphicsDeviceRef_createSampler(
		twm->device, anisotropicSampler, false, NULL, samplerNames[2], &twm->anisotropic
	))

	//Load all sections in rt_core

	gotoIfError3(clean, File_loadVirtual(CharString_createRefCStrConst("//rt_core"), NULL, e_rr))

	Log_debugLnx("Create images");

	{
		//Normal crabbage

		CharString path = CharString_createRefCStrConst("//rt_core/images/crabbage.bmp");
		gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))

		BMPInfo bmpInfo;
		gotoIfError2(clean, BMP_readx(tempBuffers[0], &bmpInfo, &tempBuffers[2]))

		if(bmpInfo.w >> 16 || bmpInfo.h >> 16)
			retError(clean, Error_invalidState(0, "onManagerCreate() bmpInfo resolution out of bounds"))

		gotoIfError2(clean, GraphicsDeviceRef_createTexture(
			twm->device,
			ETextureType_2D,
			(ETextureFormatId) bmpInfo.textureFormatId,
			EGraphicsResourceFlag_ShaderReadBindless,
			(U16)bmpInfo.w, (U16)bmpInfo.h, 1,
			NULL,
			CharString_createRefCStrConst("Crabbage.bmp 600x"),
			&tempBuffers[2],
			&twm->crabbage2049x
		))

		Buffer_freex(&tempBuffers[0]);		//Free the file here, since it might be referenced by BMP_read
		Buffer_freex(&tempBuffers[2]);

		//DDS crabbage

		if(GraphicsDeviceRef_ptr(twm->device)->info.capabilities.dataTypes & EGraphicsDataTypes_BCn) {

			path = CharString_createRefCStrConst("//rt_core/images/crabbage_mips.dds");
			gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))

			DDSInfo ddsInfo;
			gotoIfError2(clean, DDS_readx(tempBuffers[0], &ddsInfo, &subResource))

			path = CharString_createRefCStrConst("test_crabbage_mips.dds");
			gotoIfError2(clean, DDS_writex(subResource, ddsInfo, &tempBuffers[1]))
			gotoIfError3(clean, File_writex(tempBuffers[1], path, 0, 0, U64_MAX, false, e_rr))
			Buffer_freex(&tempBuffers[1]);

			gotoIfError2(clean, GraphicsDeviceRef_createTexture(
				twm->device,
				ddsInfo.type,
				ddsInfo.textureFormatId,
				EGraphicsResourceFlag_ShaderReadBindless,
				(U16)ddsInfo.w, (U16)ddsInfo.h, (U16)ddsInfo.l,
				NULL,
				CharString_createRefCStrConst("Crabbage_mips.dds"),
				&subResource.ptrNonConst[0].data,
				&twm->crabbageCompressed
			))

			ListSubResourceData_freeAllx(&subResource);
			Buffer_freex(&tempBuffers[0]);
		}

		//Oops, DDS (at least BCn compression) is unsupported, let's just pretend our existing crabbage is the big crabbage

		else {
			gotoIfError2(clean, DeviceTextureRef_inc(twm->crabbage2049x))
			twm->crabbageCompressed = twm->crabbage2049x;
		}
	}

	//Create pipelines
	//Compute pipelines

	Log_debugLnx("Create compute pipelines");

	{
		//Indirect prepare

		CharString path = CharString_createRefCStrConst("//rt_core/shaders/indirect_prepare.oiSH");
		gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))
		gotoIfError3(clean, SHFile_readx(tempBuffers[0], false, &tmpBinaries[0], e_rr))

		U32 main = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("main"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		gotoIfError3(clean, GraphicsDeviceRef_createPipelineCompute(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("Prepare indirect pipeline"),
			main,
			EPipelineFlags_None,
			NULL,
			&twm->prepareIndirectPipeline,
			e_rr
		))

		SHFile_freex(&tmpBinaries[0]);
		Buffer_freex(&tempBuffers[0]);

		//Indirect compute

		path = CharString_createRefCStrConst("//rt_core/shaders/indirect_compute.oiSH");
		gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))
		gotoIfError3(clean, SHFile_readx(tempBuffers[0], false, &tmpBinaries[0], e_rr))

		main = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("main"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		gotoIfError3(clean, GraphicsDeviceRef_createPipelineCompute(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("Indirect compute dispatch"),
			main,
			EPipelineFlags_None,
			NULL,
			&twm->indirectCompute,
			e_rr
		))

		SHFile_freex(&tmpBinaries[0]);
		Buffer_freex(&tempBuffers[0]);

		//Inline raytracing test

		if (twm->enableRt) {
			
			path = CharString_createRefCStrConst("//rt_core/shaders/raytracing_test.oiSH");
			gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))
			gotoIfError3(clean, SHFile_readx(tempBuffers[0], false, &tmpBinaries[0], e_rr))

			//TODO: Turn this into uniforms

			CharString definesArr[2];
			definesArr[0] = CharString_createRefCStrConst("X");
			definesArr[1] = CharString_createRefCStrConst("Y");

			ListCharString defines = (ListCharString) { 0 };
			gotoIfError2(clean, ListCharString_createRefConst(
				definesArr, sizeof(definesArr) / sizeof(definesArr[0]), &defines
			))

			main = GraphicsDeviceRef_getFirstShaderEntry(
				twm->device,
				tmpBinaries[0],
				CharString_createRefCStrConst("main"),
				defines,
				ESHExtension_None,
				ESHExtension_None
			);

			gotoIfError3(clean, GraphicsDeviceRef_createPipelineCompute(
				twm->device,
				tmpBinaries[0],
				CharString_createRefCStrConst("Inline raytracing test"),
				main,
				EPipelineFlags_None,
				NULL,
				&twm->inlineRaytracingTest,
				e_rr
			))

			SHFile_freex(&tmpBinaries[0]);
			Buffer_freex(&tempBuffers[0]);
		}
	}

	//Graphics pipelines

	Log_debugLnx("Create graphics pipelines");

	{
		CharString path = CharString_createRefCStrConst("//rt_core/shaders/graphics_test.oiSH");
		gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))
		gotoIfError3(clean, SHFile_readx(tempBuffers[0], false, &tmpBinaries[0], e_rr))

		path = CharString_createRefCStrConst("//rt_core/shaders/depth_test.oiSH");
		gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[1], e_rr))
		gotoIfError3(clean, SHFile_readx(tempBuffers[1], false, &tmpBinaries[1], e_rr))

		ListSHFile binaries = (ListSHFile) { 0 };
		gotoIfError2(clean, ListSHFile_createRefConst(tmpBinaries, 2, &binaries))

		U32 mainVS = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("mainVS"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		U32 mainPS = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("mainPS"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		U32 mainVSDepth = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[1],
			CharString_createRefCStrConst("mainVS"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		//Pipeline without depth stencil

		ListPipelineStage stages = (ListPipelineStage) { 0 };

		PipelineStage stageArr[2] = {
			(PipelineStage) { .binaryId = mainVS, .shFileId = 0 },
			(PipelineStage) { .binaryId = mainPS, .shFileId = 0 }
		};

		gotoIfError2(clean, ListPipelineStage_createRefConst(stageArr, 2, &stages))

		ETextureFormatId nativeFormat = _PLATFORM_TYPE == PLATFORM_ANDROID ? ETextureFormatId_RGBA8 : ETextureFormatId_BGRA8;

		PipelineGraphicsInfo info = (PipelineGraphicsInfo) {
			.vertexLayout = {
				.bufferStrides12_isInstance1 = { (U16) sizeof(VertexPosBuffer), (U16) sizeof(VertexDataBuffer) },
				.attributes = {
					(VertexAttribute) {
						.offset11 = 0,
						.bufferId4 = 0,
						.format = ETextureFormatId_RG16f,
					},
					(VertexAttribute) {
						.offset11 = 0,
						.bufferId4 = 1,
						.format = ETextureFormatId_RG16f,
					}
				}
			},
			.attachmentCountExt = 1,
			.attachmentFormatsExt = { (U8) nativeFormat },
			.depthFormatExt = EDepthStencilFormat_D16,
			.msaa = EMSAASamples_Off,
			.msaaMinSampleShading = 0.2f
		};

		gotoIfError3(clean, GraphicsDeviceRef_createPipelineGraphics(
			twm->device,
			binaries,
			&stages,
			info,
			CharString_createRefCStrConst("Test graphics pipeline"),
			EPipelineFlags_None,
			NULL,
			&twm->graphicsTest,
			e_rr
		))

		//Pipeline with depth (but still the same pixel shader)

		stageArr[0] = (PipelineStage) { .binaryId = mainVSDepth, .shFileId = 1 };
		stageArr[1] = (PipelineStage) { .binaryId = mainPS,		 .shFileId = 0 };

		gotoIfError2(clean, ListPipelineStage_createRefConst(stageArr, 2, &stages))

		info = (PipelineGraphicsInfo) {
			.depthStencil = (DepthStencilState) { .flags = EDepthStencilFlags_DepthWrite },
			.attachmentCountExt = 1,
			.attachmentFormatsExt = { (U8) nativeFormat },
			.depthFormatExt = EDepthStencilFormat_D16,
			.msaa = EMSAASamples_Off,
			.msaaMinSampleShading = 0.2f
		};

		gotoIfError3(clean, GraphicsDeviceRef_createPipelineGraphics(
			twm->device,
			binaries,
			&stages,
			info,
			CharString_createRefCStrConst("Test graphics depth pipeline"),
			EPipelineFlags_None,
			NULL,
			&twm->graphicsDepthTest,
			e_rr
		))

		gotoIfError2(clean, ListPipelineStage_createRefConst(stageArr, 2, &stages))

		info = (PipelineGraphicsInfo) {
			.depthStencil = (DepthStencilState) { .flags = EDepthStencilFlags_DepthWrite },
			.attachmentCountExt = 1,
			.attachmentFormatsExt = { (U8) nativeFormat },
			.depthFormatExt = EDepthStencilFormat_D16,
			.msaa = EMSAASamples_x4,
			.msaaMinSampleShading = 0.2f
		};

		gotoIfError3(clean, GraphicsDeviceRef_createPipelineGraphics(
			twm->device,
			binaries,
			&stages,
			info,
			CharString_createRefCStrConst("Test graphics depth pipeline MSAA"),
			EPipelineFlags_None,
			NULL,
			&twm->graphicsDepthTestMSAA,
			e_rr
		))

		SHFile_freex(&tmpBinaries[0]);
		SHFile_freex(&tmpBinaries[1]);
		Buffer_freex(&tempBuffers[0]);
		Buffer_freex(&tempBuffers[1]);
	}

	//Raytracing pipelines

	Log_debugLnx("Create raytracing pipelines");

	if (twm->enableRt) {

		CharString path = CharString_createRefCStrConst("//rt_core/shaders/raytracing_pipeline_test.oiSH");
		gotoIfError3(clean, File_readx(path, U64_MAX, 0, 0, &tempBuffers[0], e_rr))
		gotoIfError3(clean, SHFile_readx(tempBuffers[0], false, &tmpBinaries[0], e_rr))

		U32 mainMiss = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("mainMiss"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		U32 mainClosestHit = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("mainClosestHit"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		U32 mainRaygen = GraphicsDeviceRef_getFirstShaderEntry(
			twm->device,
			tmpBinaries[0],
			CharString_createRefCStrConst("mainRaygen"),
			(ListCharString) { 0 },
			ESHExtension_None,
			ESHExtension_None
		);

		PipelineStage stageArr[] = {
			(PipelineStage) { .binaryId = mainClosestHit,	.shFileId = 0 },
			(PipelineStage) { .binaryId = mainMiss,			.shFileId = 0 },
			(PipelineStage) { .binaryId = mainRaygen,		.shFileId = 0 }
		};

		PipelineRaytracingGroup hitArr[] = {
			(PipelineRaytracingGroup) { .closestHit = 0, .anyHit = U32_MAX, .intersection = U32_MAX }
		};

		PipelineRaytracingInfo info = (PipelineRaytracingInfo) {
			.flags = (U8) EPipelineRaytracingFlags_DefaultStrict,
			.maxRecursionDepth = 1
		};

		ListSHFile binaries = (ListSHFile) { 0 };
		ListPipelineStage stages = (ListPipelineStage) { 0 };
		ListPipelineRaytracingGroup hitGroups = (ListPipelineRaytracingGroup) { 0 };

		gotoIfError2(clean, ListSHFile_createRefConst(&tmpBinaries[0], 1, &binaries))
		gotoIfError2(clean, ListPipelineStage_createRefConst(stageArr, sizeof(stageArr) / sizeof(stageArr[0]), &stages))

		gotoIfError2(clean, ListPipelineRaytracingGroup_createRefConst(hitArr, sizeof(hitArr) / sizeof(hitArr[0]), &hitGroups))

		gotoIfError3(clean, GraphicsDeviceRef_createPipelineRaytracingExt(
			twm->device,
			&stages,
			binaries,
			&hitGroups,
			info,
			CharString_createRefCStrConst("Raytracing pipeline test"),
			EPipelineFlags_None,
			NULL,
			&twm->raytracingPipelineTest,
			e_rr
		))

		SHFile_freex(&tmpBinaries[0]);
		Buffer_freex(&tempBuffers[0]);
	}

	//Mesh data

	VertexPosBuffer vertexPos[] = {

		//Test quad in center

		(VertexPosBuffer) { { F32_castF16(-0.5f),	F32_castF16(-0.5f) } },
		(VertexPosBuffer) { { F32_castF16(-0.25f),	F32_castF16(-0.5f) } },
		(VertexPosBuffer) { { F32_castF16(-0.25f),	F32_castF16(-0.25f) } },
		(VertexPosBuffer) { { F32_castF16(-0.5f),	F32_castF16(-0.25f) } },

		//Test tri with indirect draw

		(VertexPosBuffer) { { F32_castF16(-1),		F32_castF16(-1) } },
		(VertexPosBuffer) { { F32_castF16(-0.75f),	F32_castF16(-1) } },
		(VertexPosBuffer) { { F32_castF16(-0.75f),	F32_castF16(-0.75f) } },

		//Test quad with indirect draw

		(VertexPosBuffer) { { F32_castF16(0.75f),	F32_castF16(0.75f) } },
		(VertexPosBuffer) { { F32_castF16(1),		F32_castF16(0.75f) } },
		(VertexPosBuffer) { { F32_castF16(1),		F32_castF16(1) } },
		(VertexPosBuffer) { { F32_castF16(0.75f),	F32_castF16(1) } },
	};

	VertexDataBuffer vertDat[] = {

		//Test quad in center

		(VertexDataBuffer) { { F32_castF16(0),		F32_castF16(0) } },
		(VertexDataBuffer) { { F32_castF16(1),		F32_castF16(0) } },
		(VertexDataBuffer) { { F32_castF16(1),		F32_castF16(1) } },
		(VertexDataBuffer) { { F32_castF16(0),		F32_castF16(1) } },

		//Test tri with indirect draw

		(VertexDataBuffer) { { F32_castF16(0),		F32_castF16(0) } },
		(VertexDataBuffer) { { F32_castF16(1),		F32_castF16(0) } },
		(VertexDataBuffer) { { F32_castF16(1),		F32_castF16(1) } },

		//Test quad with indirect draw

		(VertexDataBuffer) { { F32_castF16(0),		F32_castF16(0) } },
		(VertexDataBuffer) { { F32_castF16(1),		F32_castF16(0) } },
		(VertexDataBuffer) { { F32_castF16(1),		F32_castF16(1) } },
		(VertexDataBuffer) { { F32_castF16(0),		F32_castF16(1) } }
	};

	U16 indexDat[] = {

		//Test quad in center of screen

		0, 1, 2,
		2, 3, 0,

		//Test triangle with indirect draw

		4, 5, 6,

		//Test quad with indirect draw

		7, 8, 9,
		9, 10, 7
	};

	EDeviceBufferUsage asFlag = (EDeviceBufferUsage) 0;

	if(twm->enableRt)
		asFlag |= EDeviceBufferUsage_ASReadExt;

	EDeviceBufferUsage positionBufferAs = EDeviceBufferUsage_Vertex | asFlag;
	EDeviceBufferUsage indexBufferAs = EDeviceBufferUsage_Index | asFlag;

	Log_debugLnx("Create buffers");

	Buffer vertexData = Buffer_createRefConst(vertexPos, sizeof(vertexPos));
	CharString name = CharString_createRefCStrConst("Vertex position buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBufferData(
		twm->device, positionBufferAs, EGraphicsResourceFlag_None, NULL, name, &vertexData, &twm->vertexBuffers[0]
	))

	vertexData = Buffer_createRefConst(vertDat, sizeof(vertDat));
	name = CharString_createRefCStrConst("Vertex attribute buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBufferData(
		twm->device, EDeviceBufferUsage_Vertex, EGraphicsResourceFlag_None, NULL, name, &vertexData, &twm->vertexBuffers[1]
	))

	Buffer indexData = Buffer_createRefConst(indexDat, sizeof(indexDat));
	name = CharString_createRefCStrConst("Index buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBufferData(
		twm->device, indexBufferAs, EGraphicsResourceFlag_None, NULL, name, &indexData, &twm->indexBuffer
	))

	//Build BLASes & TLAS (only if inline RT is available)
	
	Log_debugLnx("Create BLAS/TLAS");

	if(twm->enableRt) {

		//Build BLAS around first quad

		gotoIfError2(clean, GraphicsDeviceRef_createBLASExt(
			twm->device,
			ERTASBuildFlags_DefaultBLAS,
			EBLASFlag_DisableAnyHit,
			ETextureFormatId_RG16f, 0,
			ETextureFormatId_R16u,
			(U16) sizeof(vertexPos[0]),
			(DeviceData) { .buffer = twm->vertexBuffers[0] },
			(DeviceData) { .buffer = twm->indexBuffer, .len = sizeof(U16) * 6 },
			NULL,
			CharString_createRefCStrConst("Test BLAS"),
			&twm->blas
		))

		//Make simple AABB test

		F32 aabbBuffer[] = {

			-1, -1, -1,		//min 0
			0, 0, 0,		//max 0

			0, 0, 0,		//min 1
			1, 1, 1			//max 1
		};

		Buffer aabbData = Buffer_createRefConst(aabbBuffer, sizeof(aabbBuffer));
		name = CharString_createRefCStrConst("AABB buffer");
		gotoIfError2(clean, GraphicsDeviceRef_createBufferData(
			twm->device, EDeviceBufferUsage_ASReadExt, EGraphicsResourceFlag_None, NULL, name, &aabbData, &twm->aabbs
		))

		gotoIfError2(clean, GraphicsDeviceRef_createBLASProceduralExt(
			twm->device,
			ERTASBuildFlags_DefaultBLAS,
			EBLASFlag_DisableAnyHit,
			sizeof(F32) * 3 * 2,
			0,
			(DeviceData) { .buffer = twm->aabbs },
			NULL,
			CharString_createRefCStrConst("Test BLAS AABB"),
			&twm->blasAABB
		))

		//Build TLAS around BLAS

		TLASInstanceStatic instances[1] = {
			(TLASInstanceStatic) {
				.transform = {
					{ 10, 0, 0, 0 },
					{ 0, 10, 0, 0 },
					{ 0, 0, 10, 0 }
				},
				.data = (TLASInstanceData) {
					.blasCpu = twm->blas,
					.instanceId24_mask8 = ((U32)0xFF << 24),
					.sbtOffset24_flags8 = (ETLASInstanceFlag_Default << 24)
				}
			}
		};

		ListTLASInstanceStatic instanceList = (ListTLASInstanceStatic) { 0 };
		gotoIfError2(clean, ListTLASInstanceStatic_createRefConst(
			instances, sizeof(instances) / sizeof(instances[0]), &instanceList
		))

		gotoIfError2(clean, GraphicsDeviceRef_createTLASExt(
			twm->device,
			ERTASBuildFlags_DefaultTLAS,
			NULL,
			instanceList,
			false,
			NULL,
			CharString_createRefCStrConst("Test TLAS"),
			&twm->tlas
		))
	}

	//Other shader buffers
	
	Log_debugLnx("Create shader buffers");

	name = CharString_createRefCStrConst("Test shader buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBuffer(
		twm->device, EDeviceBufferUsage_None, EGraphicsResourceFlag_ShaderRWBindless, NULL, name, sizeof(F32x4), &twm->deviceBuffer
	))

	name = CharString_createRefCStrConst("View proj matrices buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBuffer(
		twm->device,
		EDeviceBufferUsage_None, EGraphicsResourceFlag_ShaderRWBindless, NULL, name, sizeof(F32x4) * 4 * 3 * 2,
		&twm->viewProjMatrices
	))

	name = CharString_createRefCStrConst("Test indirect draw buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBuffer(
		twm->device,
		EDeviceBufferUsage_Indirect, EGraphicsResourceFlag_ShaderRWBindless,
		NULL,
		name,
		sizeof(DrawCallIndexed) * 2,
		&twm->indirectDrawBuffer
	))

	name = CharString_createRefCStrConst("Test indirect dispatch buffer");
	gotoIfError2(clean, GraphicsDeviceRef_createBuffer(
		twm->device,
		EDeviceBufferUsage_Indirect, EGraphicsResourceFlag_ShaderWriteBindless,
		NULL,
		name,
		sizeof(Dispatch),
		&twm->indirectDispatchBuffer
	))
	
	Log_debugLnx("Create command list");

	gotoIfError2(clean, GraphicsDeviceRef_createCommandList(twm->device, KIBI, 64, 64, true, &twm->asCommandList))
	CommandListRef *commandList = twm->asCommandList;

	gotoIfError2(clean, CommandListRef_begin(commandList, true, U64_MAX))

	//Prepare RTAS

	ListTransition transitionArr = (ListTransition) { 0 };
	ListCommandScopeDependency depsArr = (ListCommandScopeDependency) { 0 };

	CommandScopeDependency deps[3] = { 0 };
	gotoIfError2(clean, ListCommandScopeDependency_createRefConst(deps, 1, &depsArr))

	if(twm->enableRt) {

		depsArr.length = 0;
		if(!CommandListRef_startScope(commandList, transitionArr, 0 /* id */, depsArr).genericError) {
			gotoIfError2(clean, CommandListRef_updateBLASExt(commandList, twm->blas))
			gotoIfError2(clean, CommandListRef_updateBLASExt(commandList, twm->blasAABB))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}

		deps[0] =  (CommandScopeDependency) {
			.type = ECommandScopeDependencyType_Conditional,
			.id = 0
		};

		depsArr.length = 1;
		if(!CommandListRef_startScope(commandList, transitionArr, 1 /* id */, depsArr).genericError) {
			gotoIfError2(clean, CommandListRef_updateTLASExt(commandList, twm->tlas))
			gotoIfError2(clean, CommandListRef_endScope(commandList))
		}
	}

	gotoIfError2(clean, CommandListRef_end(commandList))

	//Record commands

	gotoIfError2(clean, GraphicsDeviceRef_createCommandList(twm->device, 2 * KIBI, 64, 64, true, &twm->prepCommandList))
	commandList = twm->prepCommandList;

	gotoIfError2(clean, CommandListRef_begin(commandList, true, U64_MAX))

	typedef enum EScopes {
		EScopes_PrepareIndirect,
		EScopes_IndirectCalcConstant
	} EScopes;

	EScopes scopes; (void)scopes;

	//Prepare 2 indirect draw calls and update constant color

	Transition transitions[3] = {
		(Transition) {
			.resource = twm->indirectDrawBuffer,
			.range = { .buffer = (BufferRange) { 0 } },
			.stage = EPipelineStage_Compute,
			.isWrite = true
		},
		(Transition) {
			.resource = twm->indirectDispatchBuffer,
			.range = { .buffer = (BufferRange) { 0 } },
			.stage = EPipelineStage_Compute,
			.isWrite = true
		},
		(Transition) {
			.resource = twm->viewProjMatrices,
			.range = { .buffer = (BufferRange) { 0 } },
			.stage = EPipelineStage_Compute,
			.isWrite = true
		}
	};

	gotoIfError2(clean, ListTransition_createRefConst(transitions, 3, &transitionArr))
	depsArr.length = 0;

	if(!CommandListRef_startScope(commandList, transitionArr, EScopes_PrepareIndirect, depsArr).genericError) {
		gotoIfError2(clean, CommandListRef_setComputePipeline(commandList, twm->prepareIndirectPipeline))
		gotoIfError2(clean, CommandListRef_dispatch1D(commandList, 1))
		gotoIfError2(clean, CommandListRef_endScope(commandList))
	}

	//Test indirect compute pipeline

	transitions[0] = (Transition) {
		.resource = twm->deviceBuffer,
		.range = { .buffer = (BufferRange) { 0 } },
		.stage = EPipelineStage_Compute,
		.isWrite = true
	};

	transitionArr.length = 1;

	deps[0] = (CommandScopeDependency) {
		.type = ECommandScopeDependencyType_Conditional,
		.id = EScopes_PrepareIndirect
	};

	depsArr.length = 1;

	if(!CommandListRef_startScope(commandList, transitionArr, EScopes_IndirectCalcConstant, depsArr).genericError) {
		gotoIfError2(clean, CommandListRef_setComputePipeline(commandList, twm->indirectCompute))
		gotoIfError2(clean, CommandListRef_dispatchIndirect(commandList, twm->indirectDispatchBuffer, 0))
		gotoIfError2(clean, CommandListRef_endScope(commandList))
	}

	gotoIfError2(clean, CommandListRef_end(commandList))

	Log_debugLnx("Init success");

clean:

	ListSubResourceData_freeAllx(&subResource);

	for(U64 i = 0; i < sizeof(tempBuffers) / sizeof(tempBuffers[0]); ++i)
		Buffer_freex(&tempBuffers[i]);

	for(U64 i = 0; i < sizeof(tmpBinaries) / sizeof(tmpBinaries[0]); ++i)
		SHFile_freex(&tmpBinaries[i]);

	if(!s_uccess)
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onManagerDestroy(WindowManager *manager) {

	TestWindowManager *twm = (TestWindowManager*) manager->extendedData.ptr;

	//Delete objects

	DeviceBufferRef_dec(&twm->aabbs);
	DeviceBufferRef_dec(&twm->vertexBuffers[0]);
	DeviceBufferRef_dec(&twm->vertexBuffers[1]);
	DeviceBufferRef_dec(&twm->indexBuffer);
	DeviceBufferRef_dec(&twm->deviceBuffer);
	DeviceBufferRef_dec(&twm->viewProjMatrices);
	DeviceBufferRef_dec(&twm->indirectDrawBuffer);
	DeviceBufferRef_dec(&twm->indirectDispatchBuffer);

	DeviceTextureRef_dec(&twm->crabbage2049x);
	DeviceTextureRef_dec(&twm->crabbageCompressed);

	PipelineRef_dec(&twm->graphicsTest);
	PipelineRef_dec(&twm->graphicsDepthTest);
	PipelineRef_dec(&twm->graphicsDepthTestMSAA);
	PipelineRef_dec(&twm->prepareIndirectPipeline);
	PipelineRef_dec(&twm->indirectCompute);
	PipelineRef_dec(&twm->inlineRaytracingTest);
	PipelineRef_dec(&twm->raytracingPipelineTest);
	CommandListRef_dec(&twm->prepCommandList);
	CommandListRef_dec(&twm->asCommandList);

	ListCommandListRef_freex(&twm->commandLists);
	ListSwapchainRef_freex(&twm->swapchains);

	TLASRef_dec(&twm->tlas);
	BLASRef_dec(&twm->blas);
	BLASRef_dec(&twm->blasAABB);

	SamplerRef_dec(&twm->nearest);
	SamplerRef_dec(&twm->linear);
	SamplerRef_dec(&twm->anisotropic);

	//Wait for device and then delete device & instance (this also destroys all objects)

	GraphicsDeviceRef_wait(twm->device);
	GraphicsDeviceRef_dec(&twm->device);
	GraphicsInstanceRef_dec(&twm->instance);
}

Platform_defineEntrypoint() {

	Error err = Platform_create(Platform_argc, Platform_argv, Platform_getData(), NULL, true);

	if(err.genericError) {
		Error_printLnx(err);
		Platform_return(-2);
	}

	Error *e_rr = &err;
	Bool s_uccess = true;
	(void) s_uccess;
	
	WindowManagerCallbacks callbacks;
	callbacks.onDraw = onManagerDraw;
	callbacks.onUpdate = onManagerUpdate;
	callbacks.onCreate = onManagerCreate;
	callbacks.onDestroy = onManagerDestroy;

	WindowManager manager = (WindowManager) { 0 };
	gotoIfError3(clean, WindowManager_create(callbacks, sizeof(TestWindowManager), &manager, e_rr))

	Window *wind = NULL;
	gotoIfError3(clean, WindowManager_createWindow(
		&manager, renderVirtual ? EWindowType_Virtual : EWindowType_Physical,
		I32x2_zero(), EResolution_get(EResolution_FHD),
		I32x2_zero(), I32x2_zero(),
		EWindowHint_Default,
		CharString_createRefCStrConst("Rt core test"),
		TestWindow_getCallbacks(),
		EWindowFormat_AutoRGBA8,
		sizeof(TestWindow),
		&wind,
		e_rr
	))

	gotoIfError3(clean, WindowManager_wait(&manager, e_rr))		//Wait til all windows are closed and process their events

clean:
	WindowManager_free(&manager);
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
	Platform_cleanup();
	Platform_return(s_uccess ? 1 : -1);
}

void Program_exit() {
	Log_debugLnx("Clean exit");
}
