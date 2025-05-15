@echo off
setlocal enabledelayedexpansion

if NOT "%1" == "Debug" (
	if NOT "%1" == "Release" (
		if NOT "%1" == "RelWithDebInfo" (
			goto usage
		)
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

for /f "usebackq delims=" %%A in (`conan config home`) do set conanHome=%%A

if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
	set arch=arm64
) else (
	set arch=x64
)

set profile=%conanHome%/profiles/%1_%arch%
set profileRel=%conanHome%/profiles/Release_%arch%

echo [settings] > %profileRel%
echo build_type=Release >> %profileRel%
echo compiler=msvc >> %profileRel%
echo compiler.cppstd=20 >> %profileRel%
echo compiler.runtime=static >> %profileRel%
echo compiler.runtime_type=Release >> %profileRel%
echo compiler.version=194 >> %profileRel%
echo os=Windows >> %profileRel%

if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
	echo arch=armv8 >> %profileRel%
) else (
	echo arch=x86_64 >> %profileRel%
)

echo [settings] > %profile%
echo build_type=%1 >> %profile%
echo compiler=msvc >> %profile%
echo compiler.cppstd=20 >> %profile%
echo compiler.runtime=static >> %profile%
echo compiler.runtime_type=%1 >> %profile%
echo compiler.version=194 >> %profile%
echo os=Windows >> %profile%

if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
	echo arch=armv8 >> %profile%
) else (
	echo arch=x86_64 >> %profile%
)

REM Build for build tool

conan create core3/packages/nvapi -s build_type=Release --profile=%profileRel% --build=missing
conan create core3/packages/amd_ags -s build_type=Release --profile=%profileRel% --build=missing
conan create core3/packages/spirv_reflect -s build_type=Release --profile=%profileRel% --build=missing
conan create core3/packages/dxc -s build_type=Release --profile=%profileRel% --build=missing
conan create core3/packages/agility_sdk -s build_type=Release --profile=%profileRel% --build=missing
conan create core3/packages/openal_soft -s build_type=Release --profile=%profileRel% --build=missing

echo -- Building core3 for packaging

if "%4" == "True" (
	conan build core3 -s build_type=Release --profile=%profileRel%  -o "&:forceVulkan=%3" -o "&:dynamicLinkingGraphics=True" -o "&:enableSIMD=%2" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=False" --build=missing
	conan export-pkg core3 -s build_type=Release --profile=%profileRel%  -o "&:forceVulkan=%3" -o "&:dynamicLinkingGraphics=True" -o "&:enableSIMD=%2" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=False"
) else (
	conan create core3 -s build_type=Release --profile=%profileRel%  -o "&:forceVulkan=%3" -o "&:dynamicLinkingGraphics=True" -o "&:enableSIMD=%2" -o "&:enableTests=False" -o "&:enableOxC3CLI=True" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=True" -o "&:cliGraphics=False" --build=missing
)

REM Build for target

conan create core3/packages/nvapi -s build_type=%1 --profile=%profile% --build=missing
conan create core3/packages/amd_ags -s build_type=%1 --profile=%profile% --build=missing
conan create core3/packages/openal_soft -s build_type=Debug --profile=%profile% --build=missing

if "%3" == "False" (
	conan create core3/packages/agility_sdk -s build_type=%1 --profile=%profile% --build=missing
)

echo -- Building core3 for target

if "%4" == "True" (
	conan build core3 -s build_type=%1 --profile=%profile% -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" -o "&:enableTests=False" -o "&:enableOxC3CLI=False" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=False" -o "&:cliGraphics=False" --build=missing
	conan export-pkg core3 -s build_type=%1 --profile=%profile% -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" -o "&:enableTests=False" -o "&:enableOxC3CLI=False" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=False" -o "&:cliGraphics=False"
) else (
	conan create core3 -s build_type=%1 --profile=%profile% -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" -o "&:enableTests=False" -o "&:enableOxC3CLI=False" -o "&:forceFloatFallback=False" -o "&:enableShaderCompiler=False" -o "&:cliGraphics=False" --build=missing
)

conan build . -s build_type=%1 --profile=%profile% -o "&:forceVulkan=%3" -o "&:enableSIMD=%2" -o "&:dynamicLinkingGraphics=%5" !remainder!

goto :eof

:usage
	echo Usage: build [Build type: Debug/Release/RelWithDebInfo] [Enable SIMD: True/False] [Force Vulkan: True/False] [In repo compile: True/False] [Dynamic linking: True/False]
