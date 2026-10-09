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

option(NATIVE_MARCH "Compile with -march=native" ON)
include(CheckIPOSupported)
check_ipo_supported(RESULT NATIVE_IPO_OK OUTPUT NATIVE_IPO_ERROR LANGUAGES C)
set(CMAKE_C_COMPILE_OPTIONS_IPO -flto=auto -ffat-lto-objects)

add_library(native_core STATIC ${NATIVE_SOURCES})
if(NATIVE_IPO_OK)
    set_property(TARGET native_core PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
else()
    message(WARNING "native_core: LTO unavailable: ${NATIVE_IPO_ERROR}")
endif()
if(NATIVE_MARCH)
    target_compile_options(native_core PRIVATE -march=native)
endif()
target_include_directories(native_core PUBLIC ${NATIVE_ROOT}/include ${NATIVE_ROOT}/data)
target_compile_options(native_core PRIVATE -O3 -w)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(native_core PRIVATE -ftls-model=initial-exec)
endif()
set_target_properties(native_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
