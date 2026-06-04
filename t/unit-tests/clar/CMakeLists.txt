include(CheckFunctionExists)

cmake_minimum_required(VERSION 3.16..3.29)

project(clar LANGUAGES C)

option(BUILD_EXAMPLE "Build the example." ON)

check_function_exists(realpath CLAR_HAS_REALPATH)
if(CLAR_HAS_REALPATH)
	add_compile_definitions(-DCLAR_HAS_REALPATH)
endif()

add_library(clar INTERFACE)
target_sources(clar INTERFACE
	clar.c
	clar.h
	clar/fixtures.h
	clar/fs.h
	clar/print.h
	clar/sandbox.h
	clar/summary.h
)
set_target_properties(clar PROPERTIES
	C_STANDARD 90
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
	include(CTest)
	if(BUILD_TESTING)
		add_subdirectory(test)
	endif()

	if(BUILD_EXAMPLE)
		add_subdirectory(example)
	endif()
endif()
