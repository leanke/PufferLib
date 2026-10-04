# Builds vendor/pokered-native's `pokered` library as libnative_core.a without upstream's
# top-level CMake (which needs its deps/ submodules). The source list comes from upstream's
# own CMakeLists.txt files via tools/native_sources.py. See .claude/docs/vendor-pokered-native.md.
cmake_minimum_required(VERSION 3.16)
project(pokered_native_core C)

set(NATIVE_ROOT "" CACHE PATH "vendor/pokered-native checkout")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/native_sources.py ${NATIVE_ROOT}
    OUTPUT_VARIABLE NATIVE_SOURCES OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE NATIVE_RC)
if(NOT NATIVE_RC EQUAL 0 OR NATIVE_SOURCES STREQUAL "")
    message(FATAL_ERROR "could not read the pokered library sources from ${NATIVE_ROOT}")
endif()
string(REPLACE "\n" ";" NATIVE_SOURCES "${NATIVE_SOURCES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NATIVE_ROOT}/CMakeLists.txt
             ${NATIVE_ROOT}/src/CMakeLists.txt ${NATIVE_ROOT}/data/CMakeLists.txt)

add_library(native_core STATIC ${NATIVE_SOURCES})
target_include_directories(native_core PUBLIC ${NATIVE_ROOT}/include ${NATIVE_ROOT}/data)
target_compile_options(native_core PRIVATE -O3 -w)
# The thread's bound env is one load instead of a __tls_get_addr call (upstream does the same).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(native_core PRIVATE -ftls-model=initial-exec)
endif()
set_target_properties(native_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
