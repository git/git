find_package(Python COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
	COMMAND "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/generate.py" --output "${CMAKE_CURRENT_BINARY_DIR}"
	DEPENDS main.c sample.c clar_test.h
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(clar_test)
set_target_properties(clar_test PROPERTIES
	C_STANDARD 90
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

# MSVC generates all kinds of warnings. We may want to fix these in the future
# and then unconditionally treat warnings as errors.
if(NOT MSVC)
	set_target_properties(clar_test PROPERTIES
		COMPILE_WARNING_AS_ERROR ON
	)
endif()

target_sources(clar_test PRIVATE
	main.c
	sample.c
	"${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
)
target_compile_definitions(clar_test PRIVATE
	CLAR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
)
target_compile_options(clar_test PRIVATE
	$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>
)
target_include_directories(clar_test PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(clar_test clar)
