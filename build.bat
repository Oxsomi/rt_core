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

if NOT "%4" == "True" (
	if NOT "%4" == "False" (
		goto usage
	)
)

if NOT "%5" == "True" (
	if NOT "%5" == "False" (
		goto usage
	)
)

for /f "tokens=5,* delims= " %%a in ("%*") do set remainder=%%b

REM Build for build tool

conan create core3/packages/nvapi -s build_type=Release --build=missing
conan create core3/packages/amd_ags -s build_type=Release --build=missing
conan create core3/packages/spirv_reflect -s build_type=Release --build=missing
conan create core3/packages/dxc -s build_type=Release --build=missing
conan create core3/packages/agility_sdk -s build_type=Release --build=missing

if "%4" == "True" (
	conan build core3 -s build_type=Release -o "&:forceVulkan=%3" -o "&:dynamicLinkingGraphics=True" -o "&:enableSIMD=True" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=False" --build=missing
	conan export-pkg core3 -s build_type=Release -o "&:forceVulkan=%3" -o "&:dynamicLinkingGraphics=True" -o "&:enableSIMD=True" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=False"
) else (
	conan create core3 -s build_type=Release -o "&:forceVulkan=%3" -o "&:dynamicLinkingGraphics=True" -o "&:enableSIMD=True" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=False" --build=missing
)

REM Build for target

conan create core3/packages/nvapi -s build_type=%1 --build=missing
conan create core3/packages/amd_ags -s build_type=%1 --build=missing

if "%3" == "False" (
	conan create core3/packages/agility_sdk -s build_type=%1 --build=missing
)

if "%4" == "True" (
	conan build core3 -s build_type=%1 -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" -o "&:enableTests=False" -o "&:enableOxC3CLI=False" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=False" -o "&:cliGraphics=False" --build=missing
	conan export-pkg core3 -s build_type=%1 -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" -o "&:enableTests=False" -o "&:enableOxC3CLI=False" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=False" -o "&:cliGraphics=False"
) else (
	conan create core3 -s build_type=%1 -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" -o "&:enableTests=False" -o "&:enableOxC3CLI=False" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=False" -o "&:cliGraphics=False" --build=missing
)

conan build . -s build_type=%1 -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" !remainder!

goto :eof

:usage
	echo Usage: build [Build type: Debug/Release] [Enable SIMD: True/False] [Force Vulkan: True/False] [In repo compile: True/False] [Dynamic linking: True/False]
