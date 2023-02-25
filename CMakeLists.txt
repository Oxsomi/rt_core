#  MIT License
#   
#  Copyright (c) 2022 Oxsomi, Nielsbishere (Niels Brunekreef)
#  
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#  
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#  
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE. 

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

# Types 

file(GLOB_RECURSE headers "inc/*.h")
file(GLOB_RECURSE sources "src/*.c")

add_library(
	rt_core
	STATIC
	${headers}
	${sources}
	CMakeLists.txt
)

set_target_properties(rt_core PROPERTIES FOLDER Oxsomi/raytracing)

add_subdirectory(core3)

target_include_directories(rt_core PUBLIC inc/rt_core)
target_link_libraries(rt_core PUBLIC OxC3_platforms)

add_virtual_files(TARGET rt_core NAME shaders ROOT ${CMAKE_CURRENT_SOURCE_DIR}/res/shaders)

file(GLOB_RECURSE tests "tst/*.c")

add_executable(
	rt_core_tests
	${tests}
	CMakeLists.txt
)

set_target_properties(rt_core_tests PROPERTIES FOLDER Oxsomi/raytracing/test)

target_link_libraries(rt_core_tests PUBLIC rt_core)

add_virtual_dependencies(TARGET rt_core_tests DEPENDENCIES rt_core)
configure_icon(rt_core_tests "${CMAKE_CURRENT_SOURCE_DIR}/res/logo.ico")
configure_virtual_files(rt_core_tests)
