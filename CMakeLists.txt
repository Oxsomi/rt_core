# rt_core, a basic raytracing framework on top of OxC3 (Oxsomi Core 3).
# Copyright (C) 2023 - 2024 Oxsomi / Nielsbishere (Niels Brunekreef)
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see https://github.com/Oxsomi/rt_core/blob/main/LICENSE.
# Be aware that GPL3 requires closed source products to be GPL3 too if released to the public.
# To prevent this a separate license will have to be requested at contact@osomi.net for a premium;
# This is called dual licensing.

cmake_minimum_required(VERSION 3.13.0)
project(rt_core)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_SUPPRESS_REGENERATION true)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 20)				# We need some C++ to interface with DXC, but we wrap it using C
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(EnableSIMD ON CACHE BOOL "Enables SIMD")
set(ForceVulkan OFF CACHE BOOL "Force Vulkan support (disable native API if available)")

if(EnableSIMD)
	message("-- Enabling SIMD (-DEnableSIMD=ON)")
	set(SIMD 1)
else()
	message("-- Disabling SIMD (-DEnableSIMD=OFF)")
	set(SIMD 0)
endif()

if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
	message("-- Enabling ARM specific optimizations")
	set(ARM ON)
else()
	message("-- Enabling x64 specific optimizations")
	set(ARM OFF)
endif()

if(MSVC)

	add_compile_options(/MP /GR /D_CRT_SECURE_NO_WARNINGS /Gy /W4 /WX)

	if(EnableSIMD)
		if(ARM)
			add_compile_options(/arch:ARMv7VE)		# Enable ARM arch
		else()
			add_compile_options(/d2archSSE42)		# Require SSE4.2
		endif()
	endif()

	add_compile_options(/fp:strict)

else()

	if(ARM)
		add_compile_options(-mfpu=neon -march=armv8-a+crc+simd+crypto)		# We enable neon, but we don't use it in vectors yet
	else()
		if(EnableSIMD)
			add_compile_options(-mfpmath=sse -msse -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2 -mrdrnd -msha -maes -mbmi2 -mf16c)
		else()
			add_compile_options(-mfpmath=sse -msse)		# Consistent float behavior
		endif()
	endif()

	add_compile_options(-fno-unsafe-math-optimizations)
	add_compile_options(-fms-extensions)
	add_compile_options(-flto -Wall -Wextra -Werror)

	if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 4.7)
		add_compile_options(-fno-fat-lto-objects)
	endif()

endif()

find_package(oxc3 REQUIRED)

file(GLOB_RECURSE tests "tst/*.c")
file(GLOB_RECURSE includes "tst/*.h")

add_executable(
	rt_core
	${tests}
	${includes}
	CMakeLists.txt
)

target_compile_definitions(rt_core PUBLIC -D_ENABLE_SIMD=${SIMD})

target_link_libraries(rt_core PUBLIC oxc3::oxc3)
set_target_properties(rt_core PROPERTIES FOLDER Oxsomi/test)

add_virtual_files(
	TARGET rt_core NAME shaders
	ROOT ${CMAKE_CURRENT_SOURCE_DIR}/res/shaders
	SELF ${CMAKE_CURRENT_SOURCE_DIR}
	ARGS -threads "100%"
)

add_virtual_files(
	TARGET rt_core NAME images
	ROOT ${CMAKE_CURRENT_SOURCE_DIR}/res/images
	SELF ${CMAKE_CURRENT_SOURCE_DIR}
)

configure_icon(rt_core "${CMAKE_CURRENT_SOURCE_DIR}/res/logo.ico")
configure_virtual_files(rt_core)
apply_dependencies(rt_core)
