find_package(Python COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
	COMMAND "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/generate.py" --output "${CMAKE_CURRENT_BINARY_DIR}"
	DEPENDS main.c example.c
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(example)
set_target_properties(example PROPERTIES
	C_STANDARD 90
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
target_sources(example PRIVATE
	main.c
	example.c
	"${CMAKE_CURRENT_BINARY_DIR}/clar.suite"
)
target_compile_definitions(example PRIVATE)
target_compile_options(example PRIVATE
	$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall>
)
target_include_directories(example PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(example clar)
