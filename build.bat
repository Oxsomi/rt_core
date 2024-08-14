@echo off
setlocal enabledelayedexpansion

if NOT "%1" == "Debug" (
	if NOT "%1" == "Release" (
		goto usage
	)	
)

if NOT "%2" == "True" (
	if NOT "%2" == "False" (
		goto usage
	)	
)

if NOT "%3" == "True" (
	if NOT "%3" == "False" (
		goto usage
	)	
)

for /f "tokens=3,* delims= " %%a in ("%*") do set remainder=%%b

REM Build for build tool

conan create core3/external/dxc -s build_type=Release --build=missing
conan create core3 -s build_type=Release -o "&:forceVulkan=False" -o "&:enableSIMD=True" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=True" --build=missing

REM Build for target

conan create core3 -s build_type=Release -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=True" --build=missing
conan build . -s build_type=%1 -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" !remainder!

goto :eof

:usage
	echo Usage: build [Build type: Debug/Release] [Enable SIMD: True/False] [Force Vulkan: True/False]
