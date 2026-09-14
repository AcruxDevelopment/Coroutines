cmake_minimum_required(VERSION 3.20)
project(acrux_coroutines CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only module: no library target to build, just the include path.
add_library(acrux_coroutines INTERFACE)
add_library(acrux::coroutines ALIAS acrux_coroutines)

target_include_directories(acrux_coroutines INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

target_compile_features(acrux_coroutines INTERFACE cxx_std_20)

option(ACRUX_COROUTINES_BUILD_TEST "Build the test/example executable" ON)

if(ACRUX_COROUTINES_BUILD_TEST)
	enable_testing()
	add_executable(acrux_coroutines_test test/coroutines/main.cpp)
	target_link_libraries(acrux_coroutines_test PRIVATE acrux::coroutines)

	if(NOT MSVC)
		target_compile_options(acrux_coroutines_test PRIVATE -Wall -Wextra)
	endif()

	# The test writes an interactive "cutscene" to stdout and waits on
	# std::cin.get() partway through -- pipe it some input so it can run
	# unattended under ctest.
	add_test(NAME acrux_coroutines_test
		COMMAND ${CMAKE_COMMAND} -E env sh -c "printf '\\n\\n\\n' | $<TARGET_FILE:acrux_coroutines_test>")
endif()
