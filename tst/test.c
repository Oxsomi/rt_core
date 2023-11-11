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
#include "types/flp.h"
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
#include "graphics/generic/device_buffer.h"
#include <stdio.h>

const Bool Platform_useWorkingDirectory = false;

void Program_exit() { }

void onButton(Window *w, InputDevice *device, InputHandle handle, Bool isDown) {

	if(device->type != EInputDeviceType_Keyboard)
		return;

	if(isDown && handle == EKey_F11)
		Window_toggleFullScreen(w);
}

U64 framesSinceLastSecond = 0;
F64 timeSinceLastSecond = 0, time = 0;

void onUpdate(Window *w, F64 dt) {

	w;

	F64 prevTime = time;

	time += dt;

	if(F64_floor(prevTime) != F64_floor(time)) {

		Log_debugLnx("%u fps", (U32)F64_round(framesSinceLastSecond / timeSinceLastSecond));

		framesSinceLastSecond = 0;
		timeSinceLastSecond = 0;
	}

	timeSinceLastSecond += dt;
}

GraphicsInstanceRef *instance = NULL;
GraphicsDeviceRef *device = NULL;
SwapchainRef *swapchain = NULL;
CommandListRef *commandList = NULL;
DeviceBufferRef *vertexBuffers[2] = { 0 };
DeviceBufferRef *indexBuffer = NULL;
DeviceBufferRef *deviceBuffer = NULL;			//Constant F32x3
DeviceBufferRef *indirectBuffer = NULL;			//sizeof(IndirectDrawIndexed) * 2

List computeShaders;
List graphicsShaders;

void onDraw(Window *w) {

	w;

	++framesSinceLastSecond;

	Error err = Error_none();

	List commandLists = (List) { 0 };
	List swapchains = (List) { 0 };

	_gotoIfError(clean, List_createConstRef((const U8*) &commandList, 1, sizeof(commandList), &commandLists));
	_gotoIfError(clean, List_createConstRef((const U8*) &swapchain, 1, sizeof(swapchain), &swapchains));

	DeviceBuffer *deviceBuf = DeviceBufferRef_ptr(deviceBuffer);

	typedef struct RuntimeData {
		U32 constantColorRead, constantColorWrite, indirectDrawWrite;
	} RuntimeData;

	RuntimeData data = (RuntimeData) {
		.constantColorRead = deviceBuf->readHandle,
		.constantColorWrite = deviceBuf->writeHandle,
		.indirectDrawWrite = DeviceBufferRef_ptr(indirectBuffer)->writeHandle
	};
	
	Buffer runtimeData = Buffer_createConstRef(&data, sizeof(data));

	_gotoIfError(clean, GraphicsDeviceRef_submitCommands(device, commandLists, swapchains, runtimeData));

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onResize(Window *w) {

	Error err = Error_none();
	Bool hasSwapchain = I32x2_all(I32x2_gt(w->size, I32x2_zero()));

	if(hasSwapchain)
		_gotoIfError(clean, Swapchain_resize(SwapchainRef_ptr(swapchain)));

	//Record commands

	_gotoIfError(clean, CommandListRef_begin(commandList, true));

	if(hasSwapchain) {

		typedef enum EScopes {

			EScopes_PrepareIndirect,
			EScopes_GraphicsTest,
			EScopes_ComputeTest

		} EScopes;

		//Prepare 2 indirect draw calls and update constant color

		Transition transitions[2] = {
			(Transition) {
				.resource = indirectBuffer,
				.range = { .buffer = (BufferRange) { 0 } },
				.stage = EPipelineStage_Compute,
				.isWrite = true
			},
			(Transition) {
				.resource = deviceBuffer,
				.range = { .buffer = (BufferRange) { 0 } },
				.stage = EPipelineStage_Compute,
				.isWrite = true
			}
		};

		List transitionArr = (List) { 0 }, depsArr = (List) { 0 };
		_gotoIfError(clean, List_createConstRef(
			(const U8*) transitions, sizeof(transitions) / sizeof(Transition), sizeof(Transition), &transitionArr
		));

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_PrepareIndirect, depsArr).genericError) {
			_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ((PipelineRef**)computeShaders.ptr)[1]));
			_gotoIfError(clean, CommandListRef_dispatch1D(commandList, 1));
			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}

		//Test graphics pipeline

		CommandScopeDependency dep = (CommandScopeDependency) { 
			.type = ECommandScopeDependencyType_Weak, 
			.id = EScopes_PrepareIndirect 
		};

		_gotoIfError(clean, List_createConstRef((const U8*) &dep, 1, sizeof(dep), &depsArr));

		transitions[0] = (Transition) {
			.resource = deviceBuffer,
			.range = { .buffer = (BufferRange) { 0 } },
			.stage = EPipelineStage_Pixel,
			.isWrite = false
		};

		transitionArr.length = 1;

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_GraphicsTest, depsArr).genericError) {

			AttachmentInfo attachmentInfo = (AttachmentInfo) {
				.image = swapchain,
				.load = ELoadAttachmentType_Clear,
				.color = (ClearColor) { .colorf = {  1, 0, 0, 1 } }
			};

			List colors = (List) { 0 };
			_gotoIfError(clean, List_createConstRef((const U8*) &attachmentInfo, 1, sizeof(AttachmentInfo), &colors));

			_gotoIfError(clean, CommandListRef_setGraphicsPipeline(commandList, ((PipelineRef**)graphicsShaders.ptr)[0]));
			_gotoIfError(clean, CommandListRef_startRenderExt(commandList, I32x2_zero(), I32x2_zero(), colors, (List) { 0 }));
			_gotoIfError(clean, CommandListRef_setViewportAndScissor(commandList, I32x2_zero(), I32x2_zero()));

			PrimitiveBuffers primitiveBuffers = (PrimitiveBuffers) { 
				.vertexBuffers = { vertexBuffers[0], vertexBuffers[1] },
				.indexBuffer = indexBuffer
			};

			_gotoIfError(clean, CommandListRef_setPrimitiveBuffers(commandList, primitiveBuffers));
			_gotoIfError(clean, CommandListRef_drawIndexed(commandList, 6, 1));
			_gotoIfError(clean, CommandListRef_drawIndirect(commandList, indirectBuffer, 0, 0, 2, true));

			_gotoIfError(clean, CommandListRef_endRenderExt(commandList));
			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}

		//Test compute pipeline

		transitions[0] = (Transition) {
			.resource = swapchain,
			.range = { .image = (ImageRange) { 0 } },
			.stage = EPipelineStage_Compute,
			.isWrite = true
		};

		transitionArr.length = 1;

		dep = (CommandScopeDependency) { 
			.type = ECommandScopeDependencyType_Unconditional, 
			.id = EScopes_GraphicsTest 
		};

		if(!CommandListRef_startScope(commandList, transitionArr, EScopes_ComputeTest, depsArr).genericError) {

			Swapchain *swapchainPtr = SwapchainRef_ptr(swapchain);

			U32 tilesX = (U32)(I32x2_x(swapchainPtr->size) + 15) >> 4;
			U32 tilesY = (U32)(I32x2_y(swapchainPtr->size) + 15) >> 4;

			_gotoIfError(clean, CommandListRef_setComputePipeline(commandList, ((PipelineRef**)computeShaders.ptr)[0]));
			_gotoIfError(clean, CommandListRef_dispatch2D(commandList, tilesX, tilesY));
			_gotoIfError(clean, CommandListRef_endScope(commandList));
		}
	}

	_gotoIfError(clean, CommandListRef_end(commandList));

clean:
	Error_printx(err, ELogLevel_Error, ELogOptions_Default);
}

void onCreate(Window *w) {

	if(!(w->flags & EWindowFlags_IsVirtual)) {

		SwapchainInfo swapchainInfo = (SwapchainInfo) { .window = w, .usage = ESwapchainUsage_AllowCompute };

		Error err = GraphicsDeviceRef_createSwapchain(device, swapchainInfo, &swapchain);
		Error_printx(err, ELogLevel_Error, ELogOptions_Default);
	}
}

void onDestroy(Window *w) {

	if(!(w->flags & EWindowFlags_IsVirtual))
		SwapchainRef_dec(&swapchain);
}

typedef struct VertexPosBuffer {

	F16 pos[2];

} VertexPosBuffer;

typedef struct VertexDataBuffer {

	F16 uv[2];

} VertexDataBuffer;

int Program_run() {

	Error err = Error_none();
	Window *wind = NULL;
	Buffer tempShaders[2] = { 0 };

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
	graphicsShaders = (List) { 0 };

	//Create pipelines

	CharString shaders = CharString_createConstRefCStr("//rt_core/shaders");
	_gotoIfError(clean, File_loadVirtual(shaders, NULL));

	//Compute pipelines

	{

		CharString path = CharString_createConstRefCStr("//rt_core/shaders/compute_test.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[0]));

		path = CharString_createConstRefCStr("//rt_core/shaders/indirect_prepare.main");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[1]));

		//TODO: Load in tempShaders[1]

		CharString nameArr[] = {
			CharString_createConstRefCStr("Test compute pipeline"),
			CharString_createConstRefCStr("Prepare indirect pipeline")
		};

		List binaries = (List) { 0 }, names = (List) { 0 };

		_gotoIfError(clean, List_createConstRef((const U8*) &tempShaders[0], 2, sizeof(Buffer), &binaries));

		_gotoIfError(clean, List_createConstRef(
			(const U8*) &nameArr, sizeof(nameArr) / sizeof(nameArr[0]), sizeof(nameArr[0]), &names)
		);

		_gotoIfError(clean, GraphicsDeviceRef_createPipelinesCompute(device, &binaries, names, &computeShaders));

		tempShaders[0] = tempShaders[1] = Buffer_createNull();
	}

	//Graphics pipelines

	{
		CharString path = CharString_createConstRefCStr("//rt_core/shaders/graphics_test.mainVS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[0]));

		path = CharString_createConstRefCStr("//rt_core/shaders/graphics_test.mainPS");
		_gotoIfError(clean, File_read(path, U64_MAX, &tempShaders[1]));

		PipelineStage stageArr[2] = {
			(PipelineStage) {
				.stageType = EPipelineStage_Vertex,
				.shaderBinary = tempShaders[0]
			},
			(PipelineStage) {
				.stageType = EPipelineStage_Pixel,
				.shaderBinary = tempShaders[1]
			}
		};

		List stages = (List) { 0 };
		_gotoIfError(clean, List_createConstRef(
			(const U8*) stageArr, sizeof(stageArr) / sizeof(stageArr[0]), sizeof(stageArr[0]), &stages
		));
	
		PipelineGraphicsInfo infoArr[1] = {
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
			}
		};

		CharString nameArr[] = {
			CharString_createConstRefCStr("Test graphics pipeline")
		};

		List infos = (List) { 0 }, names = (List) { 0 };

		_gotoIfError(clean, List_createConstRef(
			(const U8*) infoArr, sizeof(infoArr) / sizeof(infoArr[0]), sizeof(infoArr[0]), &infos
		));

		_gotoIfError(clean, List_createConstRef(
			(const U8*) &nameArr, sizeof(nameArr) / sizeof(nameArr[0]), sizeof(nameArr[0]), &names)
		);

		_gotoIfError(clean, GraphicsDeviceRef_createPipelinesGraphics(device, &stages, &infos, names, &graphicsShaders));

		tempShaders[0] = tempShaders[1] = Buffer_createNull();
	}

	//Mesh data

	VertexPosBuffer vertexPos[] = {

		//Test quad in center

		(VertexPosBuffer) { { F32_castF16(-0.5f),	F32_castF16(-0.5f) } },
		(VertexPosBuffer) { { F32_castF16(0.5f),	F32_castF16(-0.5f) } },
		(VertexPosBuffer) { { F32_castF16(0.5f),	F32_castF16(0.5f) } },
		(VertexPosBuffer) { { F32_castF16(-0.5f),	F32_castF16(0.5f) } },

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

	Buffer vertexData = Buffer_createConstRef(vertexPos, sizeof(vertexPos));
	CharString name = CharString_createConstRefCStr("Vertex position buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		device, EDeviceBufferUsage_Vertex, name, &vertexData, &vertexBuffers[0]
	));

	vertexData = Buffer_createConstRef(vertDat, sizeof(vertDat));
	name = CharString_createConstRefCStr("Vertex attribute buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		device, EDeviceBufferUsage_Vertex, name, &vertexData, &vertexBuffers[1]
	));

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

	Buffer indexData = Buffer_createConstRef(indexDat, sizeof(indexDat));
	name = CharString_createConstRefCStr("Index buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBufferData(
		device, EDeviceBufferUsage_Index, name, &indexData, &indexBuffer
	));

	name = CharString_createConstRefCStr("Test shader buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		device, EDeviceBufferUsage_ShaderRead | EDeviceBufferUsage_ShaderWrite, name, sizeof(F32x4), &deviceBuffer
	));

	name = CharString_createConstRefCStr("Test indirect buffer");
	_gotoIfError(clean, GraphicsDeviceRef_createBuffer(
		device, 
		EDeviceBufferUsage_ShaderWrite | EDeviceBufferUsage_Indirect, 
		name, 
		sizeof(DrawCallIndexed) * 2, 
		&indirectBuffer
	));

	_gotoIfError(clean, GraphicsDeviceRef_createCommandList(device, 2 * KIBI, 64, 64, true, &commandList));

	//Setup buffer / window

	WindowManager_lock(&Platform_instance.windowManager, U64_MAX);

	WindowCallbacks callbacks = (WindowCallbacks) { 0 };
	callbacks.onDraw = onDraw;
	callbacks.onUpdate = onUpdate;
	callbacks.onDeviceButton = onButton;
	callbacks.onResize = onResize;
	callbacks.onCreate = onCreate;
	callbacks.onDestroy = onDestroy;

	_gotoIfError(clean, WindowManager_createPhysical(
		&Platform_instance.windowManager,
		I32x2_zero(), EResolution_get(EResolution_FHD),
		I32x2_zero(), I32x2_zero(),
		EWindowHint_AllowFullscreen, 
		CharString_createConstRefCStr("Rt core test"),
		callbacks,
		EWindowFormat_BGRA8,
		&wind
	));

	//Wait for user to close the window

	WindowManager_unlock(&Platform_instance.windowManager);			//We don't need to do anything now
	WindowManager_waitForExitAll(&Platform_instance.windowManager, U64_MAX);

	wind = NULL;

clean:

	Error_printx(err, ELogLevel_Error, ELogOptions_Default);

	if(wind && Lock_lock(&wind->lock, 5 * SECOND)) {
		Window_terminate(wind);
		Lock_unlock(&wind->lock);
	}

	WindowManager_unlock(&Platform_instance.windowManager);

	for(U64 i = 0; i < sizeof(tempShaders) / sizeof(tempShaders[0]); ++i)
		Buffer_freex(&tempShaders[i]);

	DeviceBufferRef_dec(&vertexBuffers[0]);
	DeviceBufferRef_dec(&vertexBuffers[1]);
	DeviceBufferRef_dec(&indexBuffer);
	DeviceBufferRef_dec(&deviceBuffer);
	DeviceBufferRef_dec(&indirectBuffer);
	PipelineRef_decAll(&graphicsShaders);
	PipelineRef_decAll(&computeShaders);
	CommandListRef_dec(&commandList);
	GraphicsDeviceRef_wait(device);
	GraphicsDeviceRef_dec(&device);
	GraphicsInstanceRef_dec(&instance);

	return 1;
}
