@echo off
setlocal enabledelayedexpansion
..\..\core3\tools\compile.bat graphics_test.hlsl vs mainVS && ..\..\core3\tools\compile.bat depth_test.hlsl vs mainVS && ..\..\core3\tools\compile.bat graphics_test.hlsl ps mainPS && ..\..\core3\tools\compile.bat indirect_prepare.hlsl cs main  && ..\..\core3\tools\compile.bat indirect_compute.hlsl cs main && ..\..\core3\tools\compile.bat raytracing_test.hlsl cs main && ..\..\core3\tools\compile.bat raytracing_pipeline_test.hlsl rt

