/* OxC3/RT Core(Oxsomi core 3/RT Core), a general framework for raytracing applications.
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

#include "platforms/ext/listx_impl.h"
#include "types/buffer.h"
#include "types/string.h"
#include "types/time.h"
#include "types/flp.h"
#include "formats/bmp.h"
#include "formats/dds.h"
#include "platforms/keyboard.h"
#include "platforms/platform.h"
#include "types/thread.h"
#include "platforms/input_device.h"
#include "platforms/window_manager.h"
#include "platforms/window.h"
#include "platforms/log.h"
#include "platforms/file.h"
#include "platforms/ext/errorx.h"
#include "platforms/ext/bufferx.h"
#include "platforms/ext/stringx.h"
#include "platforms/ext/bmpx.h"
#include "platforms/ext/ddsx.h"
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

const Bool Platform_useWorkingDirectory = false;

//Globals

typedef struct TestWindowManager {

	GraphicsInstanceRef *instance;
	GraphicsDeviceRef *device;
	CommandListRef *prepCommandList;

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

	ListPipelineRef computeShaders;
	ListPipelineRef graphicsShaders;
	ListPipelineRef raytracingShaders;
	ListCommandListRef commandLists;
	ListSwapchainRef swapchains;

	SamplerRef *linear, *nearest, *anisotropic;

	U64 framesSinceLastSecond;
	F64 timeSinceLastSecond, time, timeSinceLastRender, realTime;

	F32 timeStep;
	Bool renderVirtual;
	Bool enableRt;
	U8 pad[2];

	Ns lastTime;

	F64 JD;

	F32x4 camPos;

} TestWindowManager;

//Per window data

typedef struct TestWindow {

	F64 time;
	CommandListRef *commandList;
	RefPtr *swapchain;					//Can be either SwapchainRef (non virtual) or RenderTextureRef (virtual)
	DepthStencilRef *depthStencil;
	RenderTextureRef *renderTexture;

} TestWindow;

void onDraw(Window *w);
void onUpdate(Window *w, F64 dt);
void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown);
void onResize(Window *w);
void onCreate(Window *w);
void onDestroy(Window *w);
void onTypeChar(Window *w, CharString str);

WindowCallbacks TestWindow_getCallbacks() {
	WindowCallbacks callbacks = (WindowCallbacks) { 0 };
	callbacks.onDraw = onDraw;
	callbacks.onUpdate = onUpdate;
	callbacks.onTypeChar = onTypeChar;
	callbacks.onDeviceButton = onButton;
	callbacks.onResize = onResize;
	callbacks.onCreate = onCreate;
	callbacks.onDestroy = onDestroy;
	return callbacks;
}

//Functions

static const F32 timestep = 10000;

void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown) {

	if(device->type != EInputDeviceType_Keyboard)
		return;

	TestWindowManager *twm = (TestWindowManager*)w->owner->extendedData.ptr;

	if(isDown) {

		CharString str = Keyboard_remap((EKey) handle);
		Log_debugLnx("Key press: %s\n", str.ptr);
		CharString_freex(&str);

		switch ((EKey) handle) {

			//F9 we pause

			case EKey_F9: {
				F32 *ts = &twm->timeStep;
				*ts = *ts == 0 ? timestep : 0;
				break;
			}

			//F11 we toggle full screen

			case EKey_F11:
				Window_toggleFullScreen(w);
				break;

			//F10 we spawn more windows (but only if multiple physical windows are supported, such as on desktop)

			case EKey_F10: {

				Window *wind = NULL;
				WindowManager_createWindow(
					w->owner, EWindowType_Physical,
					I32x2_zero(), EResolution_get(EResolution_FHD),
					I32x2_zero(), I32x2_zero(),
					EWindowHint_AllowFullscreen,
					CharString_createRefCStrConst("Rt core test (duped)"),
					TestWindow_getCallbacks(),
					EWindowFormat_BGRA8,
					sizeof(TestWindow),
					&wind
				);

				break;
			}

			default:
				break;
		}
	}
}

void onTypeChar(Window *w, CharString str) {
	(void) w;
	Log_debugLnx("%s", str.ptr);
}

F32 targetFps = 60;		//Only if virtual window (indicates timestep)

void onUpdate(Window *w, F64 dt) {

	if(w->type == EWindowType_Physical)
		((TestWindow*)w->extendedData.ptr)->time += dt;

	else ((TestWindow*)w->extendedData.ptr)->time += 1 / targetFps;

	//Check for keys

	F32x4 delta = F32x4_zero();
	Bool anyShiftDown = false;

	for (U64 i = 0; i < w->devices.length; ++i) {

		InputDevice id = w->devices.ptr[i];

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

	F64 prevTime = tw->realTime;
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

	Error err = Error_none();

	_gotoIfError(clean, ListCommandListRef_clear(&twm->commandLists));
	_gotoIfError(clean, ListSwapchainRef_clear(&twm->swapchains));
	_gotoIfError(clean, ListCommandListRef_reservex(&twm->commandLists, windowManager->windows.length + 1));
	_gotoIfError(clean, ListSwapchainRef_reservex(&twm->swapchains, windowManager->windows.length));

	_gotoIfError(clean, ListCommandListRef_pushBackx(&twm->commandLists, twm->prepCommandList));

	RenderTextureRef *renderTex = NULL;

	for(U64 handle = 0; handle < windowManager->windows.length; ++handle) {

		Window *w = windowManager->windows.ptr[handle];
		Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));

		if (hasSwapchain) {

			TestWindow *tw = (TestWindow*) w->extendedData.ptr;
			CommandListRef *cmd = tw->commandList;
			RefPtr *swap = tw->swapchain;

			if(!renderTex)
				renderTex = tw->renderTexture;

			_gotoIfError(clean, ListCommandListRef_pushBackx(&twm->commandLists, cmd));

			if (swap->typeId == (ETypeId) EGraphicsTypeId_Swapchain)
				_gotoIfError(clean, ListSwapchainRef_pushBackx(&twm->swapchains, swap));
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
		U32 padding0;

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
		.skyDir = { F32x4_x(skyDir), F32x4_y(skyDir), F32x4_z(skyDir) },
		.camPos = { F32x4_x(camPos), F32x4_y(camPos), F32x4_z(camPos) }
	};

	if (twm->tlas)
		data.tlasExt = TLASRef_ptr(twm->tlas)->handle;

	Buffer runtimeData = Buffer_createRefConst((const U32*)&data, sizeof(data));
	_gotoIfError(clean, GraphicsDeviceRef_submitCommands(
		twm->device, twm->commandLists, twm->swapchains, runtimeData,
		(F32)(twm->time - twm->timeSinceLastRender), (F32)twm->time
	));

	twm->timeSinceLastRender = twm->time;

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onResize(Window *w) {

	TestWindowManager *twm = (TestWindowManager*) w->owner->extendedData.ptr;
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	CommandListRef *commandList = tw->commandList;

	Error err = Error_none();
	Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));

	if(w->type != EWindowType_Virtual)
		_gotoIfError(clean, GraphicsDeviceRef_wait(twm->device));

	if(!hasSwapchain) {

		_gotoIfError(cleanTemp, CommandListRef_begin(commandList, true, U64_MAX));
		_gotoIfError(cleanTemp, CommandListRef_end(commandList));

	cleanTemp:
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
		return;
	}

	if(w->type != EWindowType_Virtual)
		_gotoIfError(clean, SwapchainRef_resize(tw->swapchain))

	//Resize depth stencil and MSAA textures

	if(tw->depthStencil)
		DepthStencilRef_dec(&tw->depthStencil);

	U16 width = (U16) I32x2_x(w->size);
	U16 height = (U16) I32x2_y(w->size);

	_gotoIfError(clean, GraphicsDeviceRef_createDepthStencil(
		twm->device,
		width, height, EDepthStencilFormat_D16, false,
		EMSAASamples_Off,
		CharString_createRefCStrConst("Test depth stencil"),
		&tw->depthStencil
	));

	if(tw->renderTexture)
		RefPtr_dec(&tw->renderTexture);

	_gotoIfError(clean, GraphicsDeviceRef_createRenderTexture(
		twm->device,
		ETextureType_2D, width, height, 1, ETextureFormatId_BGRA8, EGraphicsResourceFlag_ShaderRW,
		EMSAASamples_Off,
		CharString_createRefCStrConst("Render texture"),
		&tw->renderTexture
	));

	//Record commands

	_gotoIfError(clean, CommandListRef_begin(commandList, true, U64_MAX));

	if(hasSwapchain) {

		enum EScopes {
			EScopes_RaytracingTest,
			EScopes_RaytracingPipelineTest,
			EScopes_GraphicsTest,
			EScopes_Copy
		};

		Transition transitions[5] = { 0 };
		CommandScopeDependency deps[3] = { 0 };

		ListTransition transitionArr = (ListTransition) { 0 };
		ListCommandScopeDependency depsArr = (ListCommandScopeDependency) { 0 };
		_gotoIfError(clean, ListTransition_createRefConst(transitions, 5, &transitionArr));
		_gotoIfError(clean, ListCommandScopeDependency_createRefConst(deps, 3, &depsArr));

		//Test raytracing

		Bool hasRaytracing = twm->enableRt;

		if(hasRaytracing) {

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

			depsArr.length = 0;
			transitionArr.length = 3;

			//TODO: Enable
			if(false) { //!CommandListRef_startScope(commandList, transitionArr, EScopes_RaytracingTest, depsArr).genericError) {
				_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ListPipelineRef_at(twm->computeShaders, 2)));
				_gotoIfError(clean, CommandListRef_dispatch2D(commandList, (width + 15) >> 4, (height + 15) >> 4));
				_gotoIfError(clean, CommandListRef_endScope(commandList));
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
				_gotoIfError(clean, CommandListRef_setRaytracingPipeline(commandList, ListPipelineRef_at(twm->raytracingShaders, 0)));
				_gotoIfError(clean, CommandListRef_dispatch2DRaysExt(commandList, 0, width, height));
				_gotoIfError(clean, CommandListRef_endScope(commandList));
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

		//TODO: Re-enable
		if(false) { //!CommandListRef_startScope(commandList, transitionArr, EScopes_GraphicsTest, depsArr).genericError) {

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = tw->renderTexture,
				.unusedAfterRender = false,
				.load = hasRaytracing ? ELoadAttachmentType_Preserve : ELoadAttachmentType_Clear,
				.color = { .colorf = { 0.25f, 0.5f, 1, 1 } }
			};

			AttachmentInfo depth = (AttachmentInfo) {
				.image = tw->depthStencil,
				.unusedAfterRender = true,
				.load = ELoadAttachmentType_Clear,
				.color = (ClearColor) { .colorf = { 0 } }
			};

			ListAttachmentInfo colors = (ListAttachmentInfo) { 0 };
			_gotoIfError(clean, ListAttachmentInfo_createRefConst(&attachmentInfo, 1, &colors));

			//Start render

			_gotoIfError(clean, CommandListRef_startRenderExt(
				commandList, I32x2_zero(), I32x2_zero(), colors, depth, (AttachmentInfo) { 0 }
			));

			_gotoIfError(clean, CommandListRef_setViewportAndScissor(commandList, I32x2_zero(), I32x2_zero()));

			//Draw without depth

			_gotoIfError(clean, CommandListRef_setGraphicsPipeline(commandList, ListPipelineRef_at(twm->graphicsShaders, 0)));

			SetPrimitiveBuffersCmd primitiveBuffers = (SetPrimitiveBuffersCmd) {
				.vertexBuffers = { twm->vertexBuffers[0], twm->vertexBuffers[1] },
				.indexBuffer = twm->indexBuffer,
				.isIndex32Bit = false
			};

			_gotoIfError(clean, CommandListRef_setPrimitiveBuffers(commandList, primitiveBuffers));
			_gotoIfError(clean, CommandListRef_drawIndexed(commandList, 6, 1));
			_gotoIfError(clean, CommandListRef_drawIndirect(commandList, twm->indirectDrawBuffer, 0, 0, 2, true));

			//Draw with depth

			_gotoIfError(clean, CommandListRef_setGraphicsPipeline(commandList, ListPipelineRef_at(twm->graphicsShaders, 1)));

			_gotoIfError(clean, CommandListRef_drawUnindexed(commandList, 36, 64));		//Draw cubes

			_gotoIfError(clean, CommandListRef_endRenderExt(commandList));
			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}

		//Copy

		deps[0] = (CommandScopeDependency) { .id = EScopes_RaytracingTest };
		deps[1] = (CommandScopeDependency) { .id = EScopes_GraphicsTest };
		depsArr.length = 2;
		transitionArr.length = 0;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_Copy, depsArr).genericError) {

			_gotoIfError(clean, CommandListRef_copyImage(
				commandList, tw->renderTexture, tw->swapchain, ECopyType_All, (CopyImageRegion) { 0 }
			));

			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}
	}

	_gotoIfError(clean, CommandListRef_end(commandList));

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onCreate(Window *w) {

	TestWindowManager *twm = (TestWindowManager*) w->owner->extendedData.ptr;
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	Error err = Error_none();
	_gotoIfError(clean, GraphicsDeviceRef_createCommandList(twm->device, 2 * KIBI, 64, 64, true, &tw->commandList));

	if(w->type != EWindowType_Virtual) {
		SwapchainInfo swapchainInfo = (SwapchainInfo) { .window = w };
		_gotoIfError(clean, GraphicsDeviceRef_createSwapchain(twm->device, swapchainInfo, true, &tw->swapchain));
	}

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onDestroy(Window *w) {
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	RefPtr_dec(&tw->swapchain);
	DepthStencilRef_dec(&tw->depthStencil);
	RenderTextureRef_dec(&tw->renderTexture);
	CommandListRef_dec(&tw->commandList);
}

typedef struct VertexPosBuffer {
	F16 pos[2];
} VertexPosBuffer;

typedef struct VertexDataBuffer {
	F16 uv[2];
} VertexDataBuffer;

Bool renderVirtual = false;		//Whether or not there's a physical swapchain

void onManagerCreate(WindowManager *manager) {

	Error err = Error_none();
	Buffer tempBuffers[4] = { 0 };
	ListSubResourceData subResource = (ListSubResourceData) { 0 };

	TestWindowManager *twm = (TestWindowManager*) manager->extendedData.ptr;
	twm->timeStep = timestep;
	twm->renderVirtual = renderVirtual;
	twm->lastTime = Time_now();
	twm->JD = AtmosHelper_getJulianDate(twm->lastTime);

	//Graphics test

	GraphicsApplicationInfo applicationInfo = (GraphicsApplicationInfo) {
		.name = CharString_createRefCStrConst("Rt core test"),
		.version = GraphicsApplicationInfo_Version(0, 2, 0)
	};

	GraphicsDeviceCapabilities requiredCapabilities = (GraphicsDeviceCapabilities) { 0 };
	GraphicsDeviceInfo deviceInfo = (GraphicsDeviceInfo) { 0 };

	Bool isVerbose = false;

	_gotoIfError(clean, GraphicsInstance_create(applicationInfo, isVerbose, &twm->instance));

	_gotoIfError(clean, GraphicsInstance_getPreferredDevice(
		GraphicsInstanceRef_ptr(twm->instance),
		requiredCapabilities,
		GraphicsInstance_vendorMaskAll,
		GraphicsInstance_deviceTypeAll,
		isVerbose,
		&deviceInfo
	));

	GraphicsDeviceInfo_print(&deviceInfo, true);

	_gotoIfError(clean, GraphicsDeviceRef_create(twm->instance, &deviceInfo, isVerbose, &twm->device));

	twm->enableRt = deviceInfo.capabilities.features & (EGraphicsFeatures_RayQuery | EGraphicsFeatures_RayPipeline);

	//Create samplers

	CharString samplerNames[] = {
		CharString_createRefCStrConst("Nearest sampler"),
		CharString_createRefCStrConst("Linear sampler"),
		CharString_createRefCStrConst("Anistropic sampler")
	};

	SamplerInfo nearestSampler = (SamplerInfo) { .filter = ESamplerFilterMode_Nearest };
	SamplerInfo linearSampler = (SamplerInfo) { .filter = ESamplerFilterMode_Linear };
	SamplerInfo anisotropicSampler = (SamplerInfo) { .filter = ESamplerFilterMode_Linear, .aniso = 16 };

	_gotoIfError(clean, GraphicsDeviceRef_createSampler(twm->device, nearestSampler, samplerNames[0], &twm->nearest));
	_gotoIfError(clean, GraphicsDeviceRef_createSampler(twm->device, linearSampler, samplerNames[1], &twm->linear));
	_gotoIfError(clean, GraphicsDeviceRef_createSampler(twm->device, anisotropicSampler, samplerNames[2], &twm->anisotropic));

	//Load all sections in rt_core

	_gotoIfError(clean, File_loadVirtual(CharString_createRefCStrConst("//rt_core"), NULL));

	{
		//Normal crabbage

		CharString path = CharString_createRefCStrConst("//rt_core/images/crabbage.bmp");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[0]));

		BMPInfo bmpInfo;
		_gotoIfError(clean, BMP_readx(tempBuffers[0], &bmpInfo, &tempBuffers[2]));

		if(bmpInfo.w >> 16 || bmpInfo.h >> 16)
			_gotoIfError(clean, Error_invalidState(0, "onManagerCreate() bmpInfo resolution out of bounds"));

		_gotoIfError(clean, GraphicsDeviceRef_createTexture(
			twm->device,
			ETextureType_2D,
			(ETextureFormatId) bmpInfo.textureFormatId,
			EGraphicsResourceFlag_ShaderRead,
			(U16)bmpInfo.w, (U16)bmpInfo.h, 1,
			CharString_createRefCStrConst("Crabbage.bmp 600x"),
			&tempBuffers[2],
			&twm->crabbage2049x
		));

		Buffer_freex(&tempBuffers[0]);		//Free the file here, since it might be referenced by BMP_read
		Buffer_freex(&tempBuffers[2]);

		//DDS crabbage

		path = CharString_createRefCStrConst("//rt_core/images/crabbage_mips.dds");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[0]));

		DDSInfo ddsInfo;
		_gotoIfError(clean, DDS_readx(tempBuffers[0], &ddsInfo, &subResource));

		path = CharString_createRefCStrConst("test_crabbage_mips.dds");
		_gotoIfError(clean, DDS_writex(subResource, ddsInfo, &tempBuffers[1]));
		_gotoIfError(clean, File_write(tempBuffers[1], path, U64_MAX));
		Buffer_freex(&tempBuffers[1]);

		_gotoIfError(clean, GraphicsDeviceRef_createTexture(
			twm->device,
			ddsInfo.type,
			ddsInfo.textureFormatId,
			EGraphicsResourceFlag_ShaderRead,
			(U16)ddsInfo.w, (U16)ddsInfo.h, (U16)ddsInfo.l,
			CharString_createRefCStrConst("Crabbage_mips.dds"),
			&subResource.ptrNonConst[0].data,
			&twm->crabbageCompressed
		));

		ListSubResourceData_freeAllx(&subResource);
		Buffer_freex(&tempBuffers[0]);
	}

	//Create pipelines
	//Compute pipelines

	{
		CharString path = CharString_createRefCStrConst("//rt_core/shaders/indirect_prepare.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[0]));

		path = CharString_createRefCStrConst("//rt_core/shaders/indirect_compute.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[1]));

		path = CharString_createRefCStrConst("//rt_core/shaders/raytracing_test.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[2]));

		CharString nameArr[] = {
			CharString_createRefCStrConst("Prepare indirect pipeline"),
			CharString_createRefCStrConst("Indirect compute dispatch"),
			CharString_createRefCStrConst("Inline raytracing test")
		};

		ListBuffer binaries = (ListBuffer) { 0 };
		ListCharString names = (ListCharString) { 0 };

		_gotoIfError(clean, ListBuffer_createRefConst(tempBuffers, 3, &binaries));
		_gotoIfError(clean, ListCharString_createRefConst(nameArr, 3, &names));

		_gotoIfError(clean, GraphicsDeviceRef_createPipelinesCompute(twm->device, &binaries, names, &twm->computeShaders));

		tempBuffers[0] = tempBuffers[1] = tempBuffers[2] = Buffer_createNull();
	}

	//Graphics pipelines

	{
		CharString path = CharString_createRefCStrConst("//rt_core/shaders/graphics_test.mainVS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[0]));

		path = CharString_createRefCStrConst("//rt_core/shaders/graphics_test.mainPS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[1]));

		path = CharString_createRefCStrConst("//rt_core/shaders/depth_test.mainVS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[2]));

		PipelineStage stageArr[4] = {

			//Pipeline without depth stencil

			(PipelineStage) {
				.stageType = EPipelineStage_Vertex,
				.binary = tempBuffers[0]
			},

			(PipelineStage) {
				.stageType = EPipelineStage_Pixel,
				.binary = tempBuffers[1]
			},

			//Pipeline with depth (but still the same pixel shader)

			(PipelineStage) {
				.stageType = EPipelineStage_Vertex,
				.binary = tempBuffers[2]
			},

			(PipelineStage) {
				.stageType = EPipelineStage_Pixel,
				.binary = Buffer_createRefFromBuffer(tempBuffers[1], true)
			}
		};

		ListPipelineStage stages = (ListPipelineStage) { 0 };
		_gotoIfError(clean, ListPipelineStage_createRefConst(stageArr, sizeof(stageArr) / sizeof(stageArr[0]), &stages));

		PipelineGraphicsInfo infoArr[] = {
			(PipelineGraphicsInfo) {
				.stageCount = 2,
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
				.attachmentFormatsExt = { (U8) ETextureFormatId_BGRA8 },
				.depthFormatExt = EDepthStencilFormat_D16,
				.msaa = EMSAASamples_Off,
				.msaaMinSampleShading = 0.2f
			},
			(PipelineGraphicsInfo) {
				.depthStencil = (DepthStencilState) { .flags = EDepthStencilFlags_DepthWrite },
				.stageCount = 2,
				.attachmentCountExt = 1,
				.attachmentFormatsExt = { (U8) ETextureFormatId_BGRA8 },
				.depthFormatExt = EDepthStencilFormat_D16,
				.msaa = EMSAASamples_Off,
				.msaaMinSampleShading = 0.2f
			}
		};

		CharString nameArr[] = {
			CharString_createRefCStrConst("Test graphics pipeline"),
			CharString_createRefCStrConst("Test graphics depth pipeline")
		};

		ListPipelineGraphicsInfo infos = (ListPipelineGraphicsInfo) { 0 };
		ListCharString names = (ListCharString) { 0 };

		_gotoIfError(clean, ListPipelineGraphicsInfo_createRefConst(infoArr, sizeof(infoArr) / sizeof(infoArr[0]), &infos));
		_gotoIfError(clean, ListCharString_createRefConst(nameArr, sizeof(nameArr) / sizeof(nameArr[0]), &names));

		_gotoIfError(clean, GraphicsDeviceRef_createPipelinesGraphics(
			twm->device, &stages, &infos, names, &twm->graphicsShaders
		));

		tempBuffers[0] = tempBuffers[1] = tempBuffers[2] = Buffer_createNull();
	}

	//Raytracing pipelines

	{
		CharString path = CharString_createRefCStrConst("//rt_core/shaders/raytracing_pipeline_test.rt");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempBuffers[0]));

		PipelineStage stageArr[] = {
			(PipelineStage) { .stageType = EPipelineStage_ClosestHitExt, .binaryId = 0 },
			(PipelineStage) { .stageType = EPipelineStage_MissExt, .binaryId = 0 },
			(PipelineStage) { .stageType = EPipelineStage_RaygenExt, .binaryId = 0 }
		};

		CharString entrypointArr[] = {
			CharString_createRefCStrConst("mainClosestHit"),
			CharString_createRefCStrConst("mainMiss"),
			CharString_createRefCStrConst("mainRaygen")
		};

		PipelineRaytracingGroup hitArr[] = {
			(PipelineRaytracingGroup) { .closestHit = 0, .anyHit = U32_MAX, .intersection = U32_MAX }
		};

		CharString nameArr[] = {
			CharString_createRefCStrConst("Raytracing pipeline test")
		};

		U64 count = sizeof(nameArr) / sizeof(nameArr[0]);
		U64 entrypointCount = sizeof(stageArr) / sizeof(stageArr[0]);
		U64 hitCount = sizeof(hitArr) / sizeof(hitArr[0]);

		PipelineRaytracingInfo infoArr[] = {
			(PipelineRaytracingInfo) {

				(U8) EPipelineRaytracingFlags_Default,
				16,										//Payload size
				8,										//Attribute ssize
				1,										//Recursion

				(U32) entrypointCount,
				(U32) count,
				(U32) hitCount
			}
		};

		ListBuffer binaries = (ListBuffer) { 0 };
		ListCharString names = (ListCharString) { 0 };
		ListPipelineStage stages = (ListPipelineStage) { 0 };
		ListCharString entrypoints = (ListCharString) { 0 };
		ListPipelineRaytracingGroup hitGroups = (ListPipelineRaytracingGroup) { 0 };
		ListPipelineRaytracingInfo infos = (ListPipelineRaytracingInfo) { 0 };

		_gotoIfError(clean, ListBuffer_createRefConst(tempBuffers, count, &binaries));
		_gotoIfError(clean, ListCharString_createRefConst(nameArr, count, &names));
		_gotoIfError(clean, ListPipelineRaytracingInfo_createRefConst(infoArr, count, &infos));

		_gotoIfError(clean, ListPipelineStage_createRefConst(stageArr, entrypointCount, &stages));
		_gotoIfError(clean, ListCharString_createRefConst(entrypointArr, entrypointCount, &entrypoints));

		_gotoIfError(clean, ListPipelineRaytracingGroup_createRefConst(hitArr, hitCount, &hitGroups));

		_gotoIfError(clean, GraphicsDeviceRef_createPipelineRaytracingExt(
			twm->device, stages, &binaries, hitGroups, infos, &entrypoints, names, &twm->raytracingShaders
		));

		tempBuffers[0] = tempBuffers[1] = tempBuffers[2] = Buffer_createNull();
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

	Buffer vertexData = Buffer_createRefConst(vertexPos, sizeof(vertexPos));
	CharString name = CharString_createRefCStrConst("Vertex position buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		twm->device, positionBufferAs, EGraphicsResourceFlag_None, name, &vertexData, &twm->vertexBuffers[0]
	));

	vertexData = Buffer_createRefConst(vertDat, sizeof(vertDat));
	name = CharString_createRefCStrConst("Vertex attribute buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		twm->device, EDeviceBufferUsage_Vertex, EGraphicsResourceFlag_None, name, &vertexData, &twm->vertexBuffers[1]
	));

	Buffer indexData = Buffer_createRefConst(indexDat, sizeof(indexDat));
	name = CharString_createRefCStrConst("Index buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		twm->device, indexBufferAs, EGraphicsResourceFlag_None, name, &indexData, &twm->indexBuffer
	));

	//Build BLASes & TLAS (only if inline RT is available)

	if(twm->enableRt) {

		//Build BLAS around first quad

		_gotoIfError(clean, GraphicsDeviceRef_createBLASExt(
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
		));

		//Make simple AABB test

		F32 aabbBuffer[] = {

			-1, -1, -1,		//min 0
			0, 0, 0,		//max 0

			0, 0, 0,		//min 1
			1, 1, 1			//max 1
		};

		Buffer aabbData = Buffer_createRefConst(aabbBuffer, sizeof(aabbBuffer));
		name = CharString_createRefCStrConst("AABB buffer");
		_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
			twm->device, EDeviceBufferUsage_ASReadExt, EGraphicsResourceFlag_None, name, &aabbData, &twm->aabbs
		));

		_gotoIfError(clean, GraphicsDeviceRef_createBLASProceduralExt(
			twm->device,
			ERTASBuildFlags_DefaultBLAS,
			EBLASFlag_DisableAnyHit,
			sizeof(F32) * 3 * 2,
			0,
			(DeviceData) { .buffer = twm->aabbs },
			NULL,
			CharString_createRefCStrConst("Test BLAS AABB"),
			&twm->blasAABB
		));

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
		_gotoIfError(clean, ListTLASInstanceStatic_createRefConst(
			instances, sizeof(instances) / sizeof(instances[0]), &instanceList
		));

		_gotoIfError(clean, GraphicsDeviceRef_createTLASExt(
			twm->device,
			ERTASBuildFlags_DefaultTLAS,
			NULL,
			instanceList,
			CharString_createRefCStrConst("Test TLAS"),
			&twm->tlas
		));
	}

	//Other shader buffers

	name = CharString_createRefCStrConst("Test shader buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device, EDeviceBufferUsage_None, EGraphicsResourceFlag_ShaderRW, name, sizeof(F32x4), &twm->deviceBuffer
	));

	name = CharString_createRefCStrConst("View proj matrices buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device,
		EDeviceBufferUsage_None, EGraphicsResourceFlag_ShaderRW, name, sizeof(F32x4) * 4 * 3 * 2,
		&twm->viewProjMatrices
	));

	name = CharString_createRefCStrConst("Test indirect draw buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device,
		EDeviceBufferUsage_Indirect, EGraphicsResourceFlag_ShaderWrite,
		name,
		sizeof(DrawCallIndexed) * 2,
		&twm->indirectDrawBuffer
	));

	name = CharString_createRefCStrConst("Test indirect dispatch buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device,
		EDeviceBufferUsage_Indirect, EGraphicsResourceFlag_ShaderWrite,
		name,
		sizeof(Dispatch),
		&twm->indirectDispatchBuffer
	));

	_gotoIfError(clean, GraphicsDeviceRef_createCommandList(twm->device, 2 * KIBI, 64, 64, true, &twm->prepCommandList));

	CommandListRef *commandList = twm->prepCommandList;

	//Record commands

	_gotoIfError(clean, CommandListRef_begin(commandList, true, U64_MAX));

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

	ListTransition transitionArr = (ListTransition) { 0 };
	ListCommandScopeDependency depsArr = (ListCommandScopeDependency) { 0 };
	_gotoIfError(clean, ListTransition_createRefConst(transitions, 3, &transitionArr));

	//TODO: Re-enable
	if(false) { //!CommandListRef_startScope(commandList, transitionArr, EScopes_PrepareIndirect, depsArr).genericError) {
		_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ListPipelineRef_at(twm->computeShaders, 0)));
		_gotoIfError(clean, CommandListRef_dispatch1D(commandList, 1));
		_gotoIfError(clean, CommandListRef_endScope(commandList));
	}

	//Test indirect compute pipeline

	CommandScopeDependency deps[3] = {
		(CommandScopeDependency) {
			.type = ECommandScopeDependencyType_Conditional,
			.id = EScopes_PrepareIndirect
		}
	};

	_gotoIfError(clean, ListCommandScopeDependency_createRefConst(deps, 1, &depsArr));

	transitions[0] = (Transition) {
		.resource = twm->deviceBuffer,
		.range = { .buffer = (BufferRange) { 0 } },
		.stage = EPipelineStage_Compute,
		.isWrite = true
	};

	transitionArr.length = 1;

	//TODO: Re-enable
	if(false) { //!CommandListRef_startScope(commandList, transitionArr, EScopes_IndirectCalcConstant, depsArr).genericError) {
		_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ListPipelineRef_at(twm->computeShaders, 1)));
		_gotoIfError(clean, CommandListRef_dispatchIndirect(commandList, twm->indirectDispatchBuffer, 0));
		_gotoIfError(clean, CommandListRef_endScope(commandList));
	}

	_gotoIfError(clean, CommandListRef_end(commandList));

clean:

	ListSubResourceData_freeAllx(&subResource);

	for(U64 i = 0; i < sizeof(tempBuffers) / sizeof(tempBuffers[0]); ++i)
		Buffer_freex(&tempBuffers[i]);

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

	PipelineRef_decAll(&twm->graphicsShaders);
	PipelineRef_decAll(&twm->computeShaders);
	PipelineRef_decAll(&twm->raytracingShaders);
	CommandListRef_dec(&twm->prepCommandList);

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

I32 Program_run() {

	Error err = Error_none();

	WindowManagerCallbacks callbacks;
	callbacks.onDraw = onManagerDraw;
	callbacks.onUpdate = onManagerUpdate;
	callbacks.onCreate = onManagerCreate;
	callbacks.onDestroy = onManagerDestroy;

	WindowManager manager = (WindowManager) { 0 };
	_gotoIfError(clean, WindowManager_create(callbacks, sizeof(TestWindowManager), &manager));

	Window *wind = NULL;
	_gotoIfError(clean, WindowManager_createWindow(
		&manager, renderVirtual ? EWindowType_Virtual : EWindowType_Physical,
		I32x2_zero(), EResolution_get(EResolution_FHD),
		I32x2_zero(), I32x2_zero(),
		EWindowHint_AllowFullscreen,
		CharString_createRefCStrConst("Rt core test"),
		TestWindow_getCallbacks(),
		EWindowFormat_BGRA8,
		sizeof(TestWindow),
		&wind
	));

	_gotoIfError(clean, WindowManager_wait(&manager));		//Wait til all windows are closed and process their events

clean:
	WindowManager_free(&manager);
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
	return err.genericError ? -1 : 1;
}

void Program_exit() { }
