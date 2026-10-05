cmake_minimum_required(VERSION 3.20)
project(RandXConsumerValidation LANGUAGES CXX)

enable_testing()

set(RANDX_CONSUMER_STANDARD "" CACHE STRING "C++ standard used by this consumer")
set(RANDX_EXPECTED_CONFIG_DIR "" CACHE PATH "Directory containing the selected RandXConfig.cmake")
set(RANDX_EXPECTED_INCLUDE_ROOT "" CACHE PATH "Root containing the selected RandX header")

if(NOT RANDX_CONSUMER_STANDARD STREQUAL "17" AND NOT RANDX_CONSUMER_STANDARD STREQUAL "23")
    message(FATAL_ERROR "RANDX_CONSUMER_STANDARD must be 17 or 23")
endif()

if(RANDX_EXPECTED_CONFIG_DIR STREQUAL "" OR RANDX_EXPECTED_INCLUDE_ROOT STREQUAL "")
    message(FATAL_ERROR "The expected RandX config and include roots must be provided")
endif()

if(MSVC)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++23preview")
endif()

find_package(
    RandX CONFIG REQUIRED
    PATHS "${RANDX_EXPECTED_CONFIG_DIR}"
    NO_DEFAULT_PATH
)

file(REAL_PATH "${RandX_CONFIG}" RANDX_ACTUAL_CONFIG)
file(REAL_PATH "${RANDX_EXPECTED_CONFIG_DIR}/RandXConfig.cmake" RANDX_SELECTED_CONFIG)
if(NOT RANDX_ACTUAL_CONFIG STREQUAL RANDX_SELECTED_CONFIG)
    message(FATAL_ERROR "RandX_CONFIG resolved outside the selected package directory: ${RANDX_CONFIG}")
endif()
message(STATUS "RandX_CONFIG=${RANDX_ACTUAL_CONFIG}")

if(RANDX_CONSUMER_STANDARD STREQUAL "17")
    set(RANDX_TARGET RandX::Cpp17)
    set(RANDX_HEADER RandX_Cpp17.hpp)
    set(RANDX_CXX_STANDARD 17)
    set(RANDX_STANDARD_DEFINE RANDX_CONSUMER_STANDARD_17=1)
else()
    set(RANDX_TARGET RandX::Cpp23)
    set(RANDX_HEADER RandX.hpp)
    set(RANDX_CXX_STANDARD 23)
    set(RANDX_STANDARD_DEFINE RANDX_CONSUMER_STANDARD_23=1)
endif()

if(NOT TARGET "${RANDX_TARGET}")
    message(FATAL_ERROR "The selected RandX package does not export ${RANDX_TARGET}")
endif()

get_target_property(RANDX_INCLUDE_DIRECTORIES "${RANDX_TARGET}" INTERFACE_INCLUDE_DIRECTORIES)
if(NOT RANDX_INCLUDE_DIRECTORIES)
    message(FATAL_ERROR "${RANDX_TARGET} does not export include directories")
endif()

file(REAL_PATH "${RANDX_EXPECTED_INCLUDE_ROOT}" RANDX_SELECTED_INCLUDE_ROOT)
set(RANDX_SELECTED_HEADER_FOUND FALSE)
foreach(RANDX_INCLUDE_DIRECTORY IN LISTS RANDX_INCLUDE_DIRECTORIES)
    if(RANDX_INCLUDE_DIRECTORY MATCHES "\\$<")
        message(FATAL_ERROR "${RANDX_TARGET} exports an unresolved include expression: ${RANDX_INCLUDE_DIRECTORY}")
    endif()
    if(NOT IS_ABSOLUTE "${RANDX_INCLUDE_DIRECTORY}")
        message(FATAL_ERROR "${RANDX_TARGET} exports a non-absolute include directory: ${RANDX_INCLUDE_DIRECTORY}")
    endif()

    file(REAL_PATH "${RANDX_INCLUDE_DIRECTORY}" RANDX_RESOLVED_INCLUDE_DIRECTORY)
    set(RANDX_INCLUDE_ROOT_FOR_COMPARISON "${RANDX_SELECTED_INCLUDE_ROOT}")
    set(RANDX_INCLUDE_FOR_COMPARISON "${RANDX_RESOLVED_INCLUDE_DIRECTORY}")
    if(WIN32)
        string(TOLOWER "${RANDX_INCLUDE_ROOT_FOR_COMPARISON}" RANDX_INCLUDE_ROOT_FOR_COMPARISON)
        string(TOLOWER "${RANDX_INCLUDE_FOR_COMPARISON}" RANDX_INCLUDE_FOR_COMPARISON)
    endif()

    if(NOT RANDX_INCLUDE_FOR_COMPARISON STREQUAL RANDX_INCLUDE_ROOT_FOR_COMPARISON)
        cmake_path(
            IS_PREFIX RANDX_INCLUDE_ROOT_FOR_COMPARISON
            "${RANDX_INCLUDE_FOR_COMPARISON}"
            NORMALIZE
            RANDX_INCLUDE_IS_WITHIN_ROOT
        )
        if(NOT RANDX_INCLUDE_IS_WITHIN_ROOT)
            message(FATAL_ERROR "${RANDX_TARGET} include directory is outside the selected root: ${RANDX_INCLUDE_DIRECTORY}")
        endif()
    endif()

    if(EXISTS "${RANDX_RESOLVED_INCLUDE_DIRECTORY}/${RANDX_HEADER}")
        set(RANDX_SELECTED_HEADER_FOUND TRUE)
    endif()
endforeach()

if(NOT RANDX_SELECTED_HEADER_FOUND)
    message(FATAL_ERROR "${RANDX_TARGET} does not provide ${RANDX_HEADER} below ${RANDX_SELECTED_INCLUDE_ROOT}")
endif()

get_target_property(RANDX_PLATFORM_LINKS "${RANDX_TARGET}" INTERFACE_LINK_LIBRARIES)
if(WIN32 AND NOT "${RANDX_PLATFORM_LINKS}" MATCHES "bcrypt")
    message(FATAL_ERROR "${RANDX_TARGET} does not propagate the Windows bcrypt dependency")
elseif(APPLE AND NOT "${RANDX_PLATFORM_LINKS}" MATCHES "Security")
    message(FATAL_ERROR "${RANDX_TARGET} does not propagate the macOS Security framework dependency")
endif()

add_executable(randx_consumer consumer.cpp)
if(MSVC)
    target_compile_options(randx_consumer PRIVATE /utf-8)
endif()
target_link_libraries(randx_consumer PRIVATE "${RANDX_TARGET}")
target_compile_features(randx_consumer PRIVATE cxx_std_${RANDX_CXX_STANDARD})
target_compile_definitions(randx_consumer PRIVATE "${RANDX_STANDARD_DEFINE}")
set_target_properties(randx_consumer PROPERTIES
    CXX_STANDARD ${RANDX_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

add_test(NAME randx_consumer_run COMMAND randx_consumer)
