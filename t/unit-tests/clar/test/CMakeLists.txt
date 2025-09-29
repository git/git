find_package(Python COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
	COMMAND "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/generate.py" --output "${CMAKE_CURRENT_BINARY_DIR}"
	DEPENDS main.c selftest.c
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(selftest)
set_target_properties(selftest PROPERTIES
	C_STANDARD 90
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)

# MSVC generates all kinds of warnings. We may want to fix these in the future
# and then unconditionally treat warnings as errors.
if (NOT MSVC)
	set_target_properties(selftest PROPERTIES
		COMPILE_WARNING_AS_ERROR ON
	)
endif()

target_sources(selftest PRIVATE
	main.c
	selftest.c
	"${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
)
target_compile_definitions(selftest PRIVATE
	CLAR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/expected/"
)
target_compile_options(selftest PRIVATE
	$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>
)
target_include_directories(selftest PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(selftest clar)

add_test(NAME build_selftest
	COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config "$<CONFIG>" --target selftest
)
set_tests_properties(build_selftest PROPERTIES FIXTURES_SETUP clar_test_fixture)

add_subdirectory(suites)

add_test(NAME selftest COMMAND "${CMAKE_CURRENT_BINARY_DIR}/selftest" $<TARGET_FILE_DIR:combined_suite>)
set_tests_properties(selftest PROPERTIES FIXTURES_REQUIRED clar_test_fixture)
