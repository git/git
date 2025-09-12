find_package(Python COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
	COMMAND "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/generate.py" --output "${CMAKE_CURRENT_BINARY_DIR}"
	DEPENDS main.c selftest_suite.c
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(selftest_suite)
set_target_properties(selftest_suite PROPERTIES
	C_STANDARD 90
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

# MSVC generates all kinds of warnings. We may want to fix these in the future
# and then unconditionally treat warnings as errors.
if(NOT MSVC)
	set_target_properties(selftest_suite PROPERTIES
		COMPILE_WARNING_AS_ERROR ON
	)
endif()

target_sources(selftest_suite PRIVATE
	main.c
	selftest_suite.c
	"${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
)
target_compile_definitions(selftest_suite PRIVATE
	CLAR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
	CLAR_SELFTEST
)
target_compile_options(selftest_suite PRIVATE
	$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>
)
target_include_directories(selftest_suite PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(selftest_suite clar)
