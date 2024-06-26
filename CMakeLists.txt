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
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_SUPPRESS_REGENERATION true)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
	add_compile_options(/W4 /WX /MD /MP /GR /arch:AVX2 /D_CRT_SECURE_NO_WARNINGS /Gy)
else()
	add_compile_options(-Wall -Wextra -Werror -mavx2)
endif()

add_subdirectory(core3)

get_property(graphicsApi GLOBAL PROPERTY graphicsApi)

if("${graphicsApi}" STREQUAL "vulkan")
	set("shaderBinary" "spirv")
else()
	set("shaderBinary" "dxil")
endif()

file(GLOB_RECURSE tests "tst/*.c")
file(GLOB_RECURSE includes "tst/*.h")

add_executable(
	rt_core
	${tests}
	${includes}
	CMakeLists.txt
)

target_link_libraries(rt_core PUBLIC OxC3_graphics)

set_target_properties(rt_core PROPERTIES FOLDER Oxsomi/test)

message("-- Targeting ${shaderBinary}")

add_virtual_files(
	TARGET rt_core NAME shaders
	ROOT ${CMAKE_CURRENT_SOURCE_DIR}/res/shaders/compiled/${shaderBinary}
	SELF ${CMAKE_CURRENT_SOURCE_DIR}
)

add_virtual_files(
	TARGET rt_core NAME images
	ROOT ${CMAKE_CURRENT_SOURCE_DIR}/res/images
	SELF ${CMAKE_CURRENT_SOURCE_DIR}
)

configure_icon(rt_core "${CMAKE_CURRENT_SOURCE_DIR}/res/logo.ico")
configure_virtual_files(rt_core)
