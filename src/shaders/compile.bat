@echo off
..\..\core3\tools\compile.bat graphics_test.hlsl vs_6_5 mainVS && ..\..\core3\tools\compile.bat graphics_test.hlsl ps_6_5 mainPS && ..\..\core3\tools\compile.bat compute_test.hlsl cs_6_5 main 
