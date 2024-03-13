@echo off
setlocal enabledelayedexpansion
..\..\core3\tools\compile_debug.bat graphics_test.hlsl vs mainVS && ..\..\core3\tools\compile_debug.bat depth_test.hlsl vs mainVS && ..\..\core3\tools\compile_debug.bat graphics_test.hlsl ps mainPS && ..\..\core3\tools\compile_debug.bat indirect_prepare.hlsl cs main  && ..\..\core3\tools\compile_debug.bat indirect_compute.hlsl cs main && ..\..\core3\tools\compile_debug.bat raytracing_test.hlsl cs main && ..\..\core3\tools\compile_debug.bat raytracing_pipeline_test.hlsl rt

