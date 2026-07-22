list(APPEND suites
	"combined"
	"pointer"
)

foreach(suite IN LISTS suites)
	add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${suite}/clar.suite"
		COMMAND "${Python_EXECUTABLE}"
			"${CMAKE_SOURCE_DIR}/generate.py"
			"${CMAKE_CURRENT_SOURCE_DIR}/${suite}.c"
			--output "${CMAKE_CURRENT_BINARY_DIR}/${suite}"
		DEPENDS ${suite}.c
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	)

	add_executable(${suite}_suite)
	set_target_properties(${suite}_suite PROPERTIES
		C_STANDARD 90
		C_STANDARD_REQUIRED ON
		C_EXTENSIONS OFF
	)

	# MSVC generates all kinds of warnings. We may want to fix these in the future
	# and then unconditionally treat warnings as errors.
	if(NOT MSVC)
		set_target_properties(${suite}_suite PROPERTIES
			COMPILE_WARNING_AS_ERROR ON
		)
	endif()

	target_sources(${suite}_suite PRIVATE
		main.c
		${suite}.c
		"${CMAKE_CURRENT_BINARY_DIR}/${suite}/clar.suite"
	)
	target_compile_definitions(${suite}_suite PRIVATE
		CLAR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
		CLAR_SELFTEST
	)
	target_compile_options(${suite}_suite PRIVATE
		$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>
	)
	target_include_directories(${suite}_suite PRIVATE
		"${CMAKE_SOURCE_DIR}"
		"${CMAKE_CURRENT_BINARY_DIR}/${suite}"
	)
	target_link_libraries(${suite}_suite clar)

	add_test(NAME build_${suite}_suite
		COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config "$<CONFIG>" --target selftest
	)
	set_tests_properties(build_${suite}_suite PROPERTIES FIXTURES_SETUP clar_test_fixture)
endforeach()
