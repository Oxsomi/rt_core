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

add_subdirectory(core3)

target_include_directories(rt_core PRIVATE inc/rt_core)
target_include_directories(rt_core PRIVATE core3/inc)
target_link_libraries(rt_core PRIVATE core3_types core3_formats core3_platforms)

file(GLOB_RECURSE tests "tst/*.c")

add_executable(
	rt_core_tests
	${tests}
	CMakeLists.txt
)

target_include_directories(rt_core_tests PRIVATE inc/rt_core)
target_include_directories(rt_core_tests PRIVATE core3/inc)
target_link_libraries(rt_core_tests PRIVATE rt_core)