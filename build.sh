#!/bin/bash

usage() {
	echo Usage: build [Build type: Debug/Release] [Enable SIMD: True/False] [Force Vulkan: True/False] [In repo compile: True/False] [Dynamic linking: True/False]
	exit 1
}

if [ "$1" != Release ] && [ "$1" != Debug ]; then usage; fi
if [ "$2" != True ] && [ "$2" != False ]; then usage; fi
if [ "$3" != True ] && [ "$3" != False ]; then usage; fi
if [ "$4" != True ] && [ "$4" != False ]; then usage; fi
if [ "$5" != True ] && [ "$5" != False ]; then usage; fi

RED='\033[0;31m'
NC='\033[0m'

# Build for build tool

if ! conan create core3/packages/nvapi -s build_type=Release --build=missing; then
	printf "${RED}-- Conan create nvapi (local) failed${NC}\n"
	exit 1
fi

if ! conan create core3/packages/spirv_reflect -s build_type=Release --build=missing; then
	printf "${RED}-- Conan create spirv_reflect (local) failed${NC}\n"
	exit 1
fi

if ! conan create core3/packages/dxc -s build_type=Release --build=missing; then
	printf "${RED}-- Conan create DXC (local) failed${NC}\n"
	exit 1
fi

# if ! conan create core3 -s build_type=Release -o forceVulkan=$3 -o enableSIMD=$2 -o dynamicLinkingGraphics=True -o enableTests=False -o enableOxC3CLI=True -o forceFloatFallback=False -o enableShaderCompiler=True -o cliGraphics=False --build=missing; then
# 	printf "${RED}-- Conan install OxC3 (local) failed${NC}\n"
# 	exit 1
# fi

if ! conan create core3/packages/openal_soft -s build_type=Release --build=missing; then
	printf "${RED}-- Conan create openal_soft failed${NC}\n"
	exit 1
fi

if [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then

	if ! conan create core3/packages/xdg_shell -s build_type=Release --build=missing; then
		printf "${RED}-- Conan create xdg_shell failed${NC}\n"
		exit 1
	fi
	
	if ! conan create core3/packages/xdg_decoration -s build_type=Release --build=missing; then
		printf "${RED}-- Conan create xdg_decoration failed${NC}\n"
		exit 1
	fi
fi

if ! conan build core3 -s build_type=Release -o forceVulkan=$3 -o enableSIMD=$2 -o dynamicLinkingGraphics=True -o enableTests=False -o enableOxC3CLI=True -o forceFloatFallback=False -o enableShaderCompiler=True -o cliGraphics=False --build=missing; then
	printf "${RED}-- Conan build OxC3 (local) failed${NC}\n"
	exit 1
fi

if ! conan export-pkg core3 -s build_type=Release -o forceVulkan=$3 -o enableSIMD=$2 -o dynamicLinkingGraphics=True -o enableTests=False -o enableOxC3CLI=True -o forceFloatFallback=False -o enableShaderCompiler=True -o cliGraphics=False; then
	printf "${RED}-- Conan export-pkg OxC3 (local) failed${NC}\n"
	exit 1
fi

# Build for target

if ! conan create core3/packages/openal_soft -s build_type=$1 --build=missing; then
	printf "${RED}-- Conan create openal_soft failed${NC}\n"
	exit 1
fi

if [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then

	if ! conan create core3/packages/xdg_shell -s build_type=$1 --build=missing; then
		printf "${RED}-- Conan create xdg_shell failed${NC}\n"
		exit 1
	fi
	
	if ! conan create core3/packages/xdg_decoration -s build_type=$1 --build=missing; then
		printf "${RED}-- Conan create xdg_decoration failed${NC}\n"
		exit 1
	fi
fi

if [ "$4" != True ]; then

	if ! conan create core3 -s build_type=$1 -o forceVulkan=$3 -o enableSIMD=$2 -o dynamicLinkingGraphics=$5 -o enableTests=False -o enableOxC3CLI=False -o forceFloatFallback=False -o enableShaderCompiler=False -o cliGraphics=False --build=missing; then
		printf "${RED}-- Conan install OxC3 failed${NC}\n"
		exit 1
	fi

else

	if ! conan build core3 -s build_type=$1 -o forceVulkan=$3 -o enableSIMD=$2 -o dynamicLinkingGraphics=$5 -o enableTests=False -o enableOxC3CLI=False -o forceFloatFallback=False -o enableShaderCompiler=False -o cliGraphics=False --build=missing; then
		printf "${RED}-- Conan build OxC3 failed${NC}\n"
		exit 1
	fi

	if ! conan export-pkg core3 -s build_type=$1 -o forceVulkan=$3 -o enableSIMD=$2 -o dynamicLinkingGraphics=$5 -o enableTests=False -o enableOxC3CLI=False -o forceFloatFallback=False -o enableShaderCompiler=False -o cliGraphics=False; then
		printf "${RED}-- Conan export-pkg OxC3 failed${NC}\n"
		exit 1
	fi
fi

if ! conan build . -s build_type=$1 -o enableSIMD=$2 -o forceVulkan=$3 -o dynamicLinkingGraphics=$5 ${@:6}; then
	printf "${RED}-- Conan build failed${NC}\n"
	exit 1
fi

