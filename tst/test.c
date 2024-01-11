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

#include "platforms/ext/listx_impl.h"
#include "types/buffer.h"
#include "types/string.h"
#include "types/time.h"
#include "types/flp.h"
#include "formats/bmp.h"
#include "platforms/keyboard.h"
#include "platforms/platform.h"
#include "platforms/thread.h"
#include "platforms/input_device.h"
#include "platforms/window_manager.h"
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
#include "graphics/generic/device_buffer.h"
#include "graphics/generic/depth_stencil.h"
#include "graphics/generic/sampler.h"
#include <stdio.h>

const Bool Platform_useWorkingDirectory = false;

//Globals

typedef struct TestWindowManager {

	GraphicsInstanceRef *instance;
	GraphicsDeviceRef *device;
	CommandListRef *prepCommandList;

	DeviceBufferRef *vertexBuffers[2];
	DeviceBufferRef *indexBuffer;
	DeviceBufferRef *indirectDrawBuffer;			//sizeof(DrawCallIndexed) * 2
	DeviceBufferRef *indirectDispatchBuffer;		//sizeof(Dispatch) * 2
	DeviceBufferRef *deviceBuffer;					//Constant F32x3 for animating color
	DeviceBufferRef *viewProjMatrices;				//F32x4x4 (view, proj, viewProj)

	ListPipelineRef computeShaders;
	ListPipelineRef graphicsShaders;
	ListCommandListRef commandLists;
	ListSwapchainRef swapchains;

	SamplerRef *linear, *nearest, *anisotropic;

	U64 framesSinceLastSecond;
	F64 timeSinceLastSecond, time;

} TestWindowManager;

//Per window data

typedef struct TestWindow {

	F64 time;
	CommandListRef *commandList;
	SwapchainRef *swapchain;
	DepthStencilRef *depthStencil;

} TestWindow;

void onDraw(Window *w);
void onUpdate(Window *w, F64 dt);
void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown);
void onResize(Window *w);
void onCreate(Window *w);
void onDestroy(Window *w);

WindowCallbacks TestWindow_getCallbacks() {
	WindowCallbacks callbacks = (WindowCallbacks) { 0 };
	callbacks.onDraw = onDraw;
	callbacks.onUpdate = onUpdate;
	callbacks.onDeviceButton = onButton;
	callbacks.onResize = onResize;
	callbacks.onCreate = onCreate;
	callbacks.onDestroy = onDestroy;
	return callbacks;
}

//Functions

void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown) {

	if(device->type != EInputDeviceType_Keyboard)
		return;

	if(isDown) {

		switch ((EKey) handle) {

			//F11 we toggle full screen

			case EKey_F11:
				Window_toggleFullScreen(w);
				break;

			//F10 we spawn more windows (but only if multiple physical windows are supported, such as on desktop)

			case EKey_F10:

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
	}
}

void onUpdate(Window *w, F64 dt) {
	((TestWindow*)w->extendedData.ptr)->time += dt;
}

void onManagerUpdate(WindowManager *windowManager, F64 dt) {

	TestWindowManager *tw = (TestWindowManager*) windowManager->extendedData.ptr;

	F64 prevTime = tw->time;
	tw->time += dt;

	if(F64_floor(prevTime) != F64_floor(tw->time)) {

		Log_debugLnx("%u fps", (U32)F64_round(tw->framesSinceLastSecond / tw->timeSinceLastSecond));

		tw->framesSinceLastSecond = 0;
		tw->timeSinceLastSecond = 0;
	}

	tw->timeSinceLastSecond += dt;
}

void onDraw(Window *w) { w; }

void onManagerDraw(WindowManager *windowManager) {

	TestWindowManager *twm = (TestWindowManager*) windowManager->extendedData.ptr;
	++twm->framesSinceLastSecond;

	Error err = Error_none();

	_gotoIfError(clean, ListCommandListRef_clear(&twm->commandLists));
	_gotoIfError(clean, ListSwapchainRef_clear(&twm->swapchains));
	_gotoIfError(clean, ListCommandListRef_reservex(&twm->commandLists, windowManager->windows.length + 1));
	_gotoIfError(clean, ListSwapchainRef_reservex(&twm->swapchains, windowManager->windows.length));

	_gotoIfError(clean, ListCommandListRef_pushBackx(&twm->commandLists, twm->prepCommandList));

	for(U64 handle = 0; handle < windowManager->windows.length; ++handle) {
		
		Window *w = windowManager->windows.ptr[handle];
		Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));

		if (hasSwapchain) {

			TestWindow *tw = (TestWindow*) w->extendedData.ptr;
			CommandListRef *cmd = tw->commandList;
			RefPtr *swap = tw->swapchain;

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
	} RuntimeData;

	DeviceBuffer *viewProjMatrices = DeviceBufferRef_ptr(twm->viewProjMatrices);

	RuntimeData data = (RuntimeData) {
		.constantColorRead = deviceBuf->readHandle,
		.constantColorWrite = deviceBuf->writeHandle,
		.indirectDrawWrite = DeviceBufferRef_ptr(twm->indirectDrawBuffer)->writeHandle,
		.indirectDispatchWrite = DeviceBufferRef_ptr(twm->indirectDispatchBuffer)->writeHandle,
		.viewProjMatricesWrite = viewProjMatrices->writeHandle,
		.viewProjMatricesRead = viewProjMatrices->readHandle
	};

	Buffer runtimeData = Buffer_createRefConst((const U32*)&data, sizeof(data));
	_gotoIfError(clean, GraphicsDeviceRef_submitCommands(twm->device, twm->commandLists, twm->swapchains, runtimeData));

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onResize(Window *w) {

	TestWindowManager *twm = (TestWindowManager*) w->owner->extendedData.ptr;
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	CommandListRef *commandList = tw->commandList;
	SwapchainRef *swapchain = tw->swapchain;

	Error err = Error_none();
	Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));

	if(!hasSwapchain) {

		_gotoIfError(cleanTemp, CommandListRef_begin(commandList, true, U64_MAX));
		_gotoIfError(cleanTemp, CommandListRef_end(commandList));

	cleanTemp:
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
		return;
	}

	_gotoIfError(clean, Swapchain_resize(SwapchainRef_ptr(swapchain)));

	//Resize depth stencil

	if(tw->depthStencil)
		DepthStencilRef_dec(&tw->depthStencil);

	_gotoIfError(clean, GraphicsDeviceRef_createDepthStencil(
		twm->device, 
		w->size, EDepthStencilFormat_D16, false, 
		CharString_createRefCStrConst("Test depth stencil"), 
		&tw->depthStencil
	));

	//Record commands

	_gotoIfError(clean, CommandListRef_begin(commandList, true, U64_MAX));

	if(hasSwapchain) {

		typedef enum EScopes {

			EScopes_GraphicsTest,
			EScopes_ComputeTest,
			EScopes_GraphicsDepthTest

		} EScopes;

		//Prepare 2 indirect draw calls and update constant color

		Transition transitions[1] = { 0 };
		CommandScopeDependency deps[1] = { 0};

		ListTransition transitionArr = (ListTransition) { 0 };
		ListCommandScopeDependency depsArr = (ListCommandScopeDependency) { 0 };
		_gotoIfError(clean, ListTransition_createRefConst(transitions, 1, &transitionArr));
		_gotoIfError(clean, ListCommandScopeDependency_createRefConst(deps, 1, &depsArr));

		//Test compute pipeline

		transitions[0] = (Transition) {
			.resource = swapchain,
			.range = { .image = (ImageRange) { 0 } },
			.stage = EPipelineStage_Compute,
			.isWrite = true
		};

		transitionArr.length = 1;

		if (!CommandListRef_startScope(
			commandList, transitionArr, EScopes_ComputeTest, (ListCommandScopeDependency) { 0 }
		).genericError) {

			Swapchain *swapchainPtr = SwapchainRef_ptr(swapchain);

			U32 tilesX = (U32)(I32x2_x(swapchainPtr->size) + 15) >> 4;
			U32 tilesY = (U32)(I32x2_y(swapchainPtr->size) + 15) >> 4;

			tilesX; tilesY;

			_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ListPipelineRef_at(twm->computeShaders, 0)));
			_gotoIfError(clean, CommandListRef_dispatch2D(commandList, tilesX, tilesY));
			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}

		//Test graphics pipeline

		deps[0] = (CommandScopeDependency) {
			.type = ECommandScopeDependencyType_Unconditional, 
			.id = EScopes_ComputeTest
		};

		depsArr.length = 1;

		transitions[0] = (Transition) {
			.resource = twm->deviceBuffer,
			.range = { .buffer = (BufferRange) { 0 } },
			.stage = EPipelineStage_Pixel,
			.isWrite = false
		};

		transitionArr.length = 1;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_GraphicsTest, depsArr).genericError) {

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = swapchain,
				.load = ELoadAttachmentType_Preserve
			};

			ListAttachmentInfo colors = (ListAttachmentInfo) { 0 };
			_gotoIfError(clean, ListAttachmentInfo_createRefConst(&attachmentInfo, 1, &colors));

			_gotoIfError(clean, CommandListRef_setGraphicsPipeline(
				commandList, ListPipelineRef_at(twm->graphicsShaders, 0)
			));

			_gotoIfError(clean, CommandListRef_startRenderExt(
				commandList, I32x2_zero(), I32x2_zero(), colors, (AttachmentInfo) { 0 }, (AttachmentInfo) { 0 }
			));

			_gotoIfError(clean, CommandListRef_setViewportAndScissor(commandList, I32x2_zero(), I32x2_zero()));

			SetPrimitiveBuffersCmd primitiveBuffers = (SetPrimitiveBuffersCmd) { 
				.vertexBuffers = { twm->vertexBuffers[0], twm->vertexBuffers[1] },
				.indexBuffer = twm->indexBuffer,
				.isIndex32Bit = false
			};

			_gotoIfError(clean, CommandListRef_setPrimitiveBuffers(commandList, primitiveBuffers));
			_gotoIfError(clean, CommandListRef_drawIndexed(commandList, 6, 1));
			_gotoIfError(clean, CommandListRef_drawIndirect(commandList, twm->indirectDrawBuffer, 0, 0, 2, true));

			_gotoIfError(clean, CommandListRef_endRenderExt(commandList));
			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}

		//Test graphics pipeline with depth stencil

		deps[0] = (CommandScopeDependency) {
			.type = ECommandScopeDependencyType_Unconditional, 
			.id = EScopes_GraphicsTest
		};

		depsArr.length = 1;

		transitions[0] = (Transition) {
			.resource = twm->viewProjMatrices,
			.range = { .buffer = (BufferRange) { 0 } },
			.stage = EPipelineStage_Vertex,
			.isWrite = false
		};

		transitionArr.length = 1;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_GraphicsDepthTest, depsArr).genericError) {

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = swapchain,
				.load = ELoadAttachmentType_Preserve
			};

			ListAttachmentInfo colors = (ListAttachmentInfo) { 0 };
			_gotoIfError(clean, ListAttachmentInfo_createRefConst(&attachmentInfo, 1, &colors));

			_gotoIfError(clean, CommandListRef_setGraphicsPipeline(
				commandList, ListPipelineRef_at(twm->graphicsShaders, 1)
			));

			AttachmentInfo depth = (AttachmentInfo) {
				.image = tw->depthStencil,
				.load = ELoadAttachmentType_Clear,
				.color = (ClearColor) { .colorf = { 0 } }
			};

			_gotoIfError(clean, CommandListRef_startRenderExt(
				commandList, I32x2_zero(), I32x2_zero(), colors, depth, (AttachmentInfo) { 0 }
			));

			_gotoIfError(clean, CommandListRef_setViewportAndScissor(commandList, I32x2_zero(), I32x2_zero()));

			_gotoIfError(clean, CommandListRef_drawUnindexed(commandList, 36, 64));		//Draw cube

			_gotoIfError(clean, CommandListRef_endRenderExt(commandList));
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

	if(w->type == EWindowType_Physical) {
		SwapchainInfo swapchainInfo = (SwapchainInfo) { .window = w, .usage = ESwapchainUsage_AllowCompute };
		_gotoIfError(clean, GraphicsDeviceRef_createSwapchain(twm->device, swapchainInfo, &tw->swapchain));
	}

	//TODO: RenderTarget into swapchain

	else {

	}

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onDestroy(Window *w) {
	TestWindow *tw = (TestWindow*) w->extendedData.ptr;
	SwapchainRef_dec(&tw->swapchain);
	DepthStencilRef_dec(&tw->depthStencil);
	CommandListRef_dec(&tw->commandList);
}

typedef struct VertexPosBuffer {

	F16 pos[2];

} VertexPosBuffer;

typedef struct VertexDataBuffer {

	F16 uv[2];

} VertexDataBuffer;

void onManagerCreate(WindowManager *manager) {

	Error err = Error_none();
	Buffer tempShaders[3] = { 0 };

	TestWindowManager *twm = (TestWindowManager*) manager->extendedData.ptr;

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

	//Create pipelines

	CharString shaders = CharString_createRefCStrConst("//rt_core/shaders");
	_gotoIfError(clean, File_loadVirtual(shaders, NULL));

	//Compute pipelines

	{
		CharString path = CharString_createRefCStrConst("//rt_core/shaders/compute_test.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[0]));

		path = CharString_createRefCStrConst("//rt_core/shaders/indirect_prepare.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[1]));

		path = CharString_createRefCStrConst("//rt_core/shaders/indirect_compute.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[2]));

		CharString nameArr[] = {
			CharString_createRefCStrConst("Test compute pipeline"),
			CharString_createRefCStrConst("Prepare indirect pipeline"),
			CharString_createRefCStrConst("Indirect compute dispatch")
		};

		ListBuffer binaries = (ListBuffer) { 0 };
		ListCharString names = (ListCharString) { 0 };

		_gotoIfError(clean, ListBuffer_createRefConst(tempShaders, 3, &binaries));
		_gotoIfError(clean, ListCharString_createRefConst(nameArr, 3, &names));

		_gotoIfError(clean, GraphicsDeviceRef_createPipelinesCompute(twm->device, &binaries, names, &twm->computeShaders));

		tempShaders[0] = tempShaders[1] = tempShaders[2] = Buffer_createNull();
	}

	//Graphics pipelines

	{
		CharString path = CharString_createRefCStrConst("//rt_core/shaders/graphics_test.mainVS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[0]));

		path = CharString_createRefCStrConst("//rt_core/shaders/graphics_test.mainPS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[1]));

		path = CharString_createRefCStrConst("//rt_core/shaders/depth_test.mainVS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[2]));

		PipelineStage stageArr[4] = {

			//Pipeline without depth stencil

			(PipelineStage) {
				.stageType = EPipelineStage_Vertex,
				.shaderBinary = tempShaders[0]
			},

			(PipelineStage) {
				.stageType = EPipelineStage_Pixel,
				.shaderBinary = tempShaders[1]
			},

			//Pipeline with depth (but still the same pixel shader)

			(PipelineStage) {
				.stageType = EPipelineStage_Vertex,
				.shaderBinary = tempShaders[2]
			},

			(PipelineStage) {
				.stageType = EPipelineStage_Pixel,
				.shaderBinary = Buffer_createRefFromBuffer(tempShaders[1], true)
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
				.attachmentFormatsExt = { (U8) ETextureFormatId_BGRA8 }
			},
			(PipelineGraphicsInfo) {
				.depthStencil = (DepthStencilState) { .flags = EDepthStencilFlags_DepthWrite },
				.stageCount = 2,
				.attachmentCountExt = 1,
				.attachmentFormatsExt = { (U8) ETextureFormatId_BGRA8 },
				.depthFormatExt = EDepthStencilFormat_D16
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

		tempShaders[0] = tempShaders[1] = tempShaders[2] = Buffer_createNull();
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

	Buffer vertexData = Buffer_createRefConst(vertexPos, sizeof(vertexPos));
	CharString name = CharString_createRefCStrConst("Vertex position buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		twm->device, EDeviceBufferUsage_Vertex, name, &vertexData, &twm->vertexBuffers[0]
	));

	vertexData = Buffer_createRefConst(vertDat, sizeof(vertDat));
	name = CharString_createRefCStrConst("Vertex attribute buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		twm->device, EDeviceBufferUsage_Vertex, name, &vertexData, &twm->vertexBuffers[1]
	));

	Buffer indexData = Buffer_createRefConst(indexDat, sizeof(indexDat));
	name = CharString_createRefCStrConst("Index buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		twm->device, EDeviceBufferUsage_Index, name, &indexData, &twm->indexBuffer
	));

	name = CharString_createRefCStrConst("Test shader buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device, EDeviceBufferUsage_ShaderRead | EDeviceBufferUsage_ShaderWrite, name, sizeof(F32x4), &twm->deviceBuffer
	));

	name = CharString_createRefCStrConst("View proj matrices buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device, 
		EDeviceBufferUsage_ShaderRead | EDeviceBufferUsage_ShaderWrite, name, sizeof(F32x4) * 4 * 3, 
		&twm->viewProjMatrices
	));

	name = CharString_createRefCStrConst("Test indirect draw buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device, 
		EDeviceBufferUsage_ShaderWrite | EDeviceBufferUsage_Indirect, 
		name, 
		sizeof(DrawCallIndexed) * 2, 
		&twm->indirectDrawBuffer
	));

	name = CharString_createRefCStrConst("Test indirect dispatch buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		twm->device, 
		EDeviceBufferUsage_ShaderWrite | EDeviceBufferUsage_Indirect, 
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

	if(!CommandListRef_startScope(commandList, transitionArr, EScopes_PrepareIndirect, depsArr).genericError) {
		_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ListPipelineRef_at(twm->computeShaders, 1)));
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

	if(!CommandListRef_startScope(commandList, transitionArr, EScopes_IndirectCalcConstant, depsArr).genericError) {
		_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ListPipelineRef_at(twm->computeShaders, 2)));
		_gotoIfError(clean, CommandListRef_dispatchIndirect(commandList, twm->indirectDispatchBuffer, 0));
		_gotoIfError(clean, CommandListRef_endScope(commandList));
	}

	_gotoIfError(clean, CommandListRef_end(commandList));

clean:

	for(U64 i = 0; i < sizeof(tempShaders) / sizeof(tempShaders[0]); ++i)
		Buffer_freex(&tempShaders[i]);

	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onManagerDestroy(WindowManager *manager) {

	TestWindowManager *twm = (TestWindowManager*) manager->extendedData.ptr;

	//Delete objects

	DeviceBufferRef_dec(&twm->vertexBuffers[0]);
	DeviceBufferRef_dec(&twm->vertexBuffers[1]);
	DeviceBufferRef_dec(&twm->indexBuffer);
	DeviceBufferRef_dec(&twm->deviceBuffer);
	DeviceBufferRef_dec(&twm->viewProjMatrices);
	DeviceBufferRef_dec(&twm->indirectDrawBuffer);
	DeviceBufferRef_dec(&twm->indirectDispatchBuffer);
	PipelineRef_decAll(&twm->graphicsShaders);
	PipelineRef_decAll(&twm->computeShaders);
	CommandListRef_dec(&twm->prepCommandList);

	ListCommandListRef_freex(&twm->commandLists);
	ListSwapchainRef_freex(&twm->swapchains);

	SamplerRef_dec(&twm->nearest);
	SamplerRef_dec(&twm->linear);
	SamplerRef_dec(&twm->anisotropic);

	//Wait for device and then delete device & instance (this also destroys all objects)

	GraphicsDeviceRef_wait(twm->device);
	GraphicsDeviceRef_dec(&twm->device);
	GraphicsInstanceRef_dec(&twm->instance);
}

int Program_run() {

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
		&manager, EWindowType_Physical,
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
