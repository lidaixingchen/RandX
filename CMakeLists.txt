cmake_minimum_required(VERSION 3.20)
project(xoshiro VERSION 1.2.0 LANGUAGES CXX)

# 纯头文件库 → INTERFACE target
add_library(xoshiro INTERFACE)
add_library(xoshiro::xoshiro ALIAS xoshiro)

target_include_directories(xoshiro INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# 安装
include(GNUInstallDirs)
install(FILES Random.hpp XoshiroCpp.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS xoshiro EXPORT xoshiroTargets)
install(EXPORT xoshiroTargets
    NAMESPACE xoshiro::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xoshiro
)

# 生成 config 文件
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/xoshiroConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/xoshiroConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xoshiro
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/xoshiroConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/xoshiroConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/xoshiroConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xoshiro
)

# 可选：构建测试和 benchmark
option(XOSHIRO_BUILD_TESTS "Build tests" OFF)
option(XOSHIRO_BUILD_BENCHMARK "Build benchmark" OFF)
option(XOSHIRO_ENABLE_COVERAGE "Enable code coverage (GCC/Clang)" OFF)

if(XOSHIRO_BUILD_TESTS)
    enable_testing()

    # 共用 doctest 头文件
    set(DOCTEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

    # C++23 测试（Random.hpp）
    add_executable(test_random test_random.cpp)
    target_compile_features(test_random PRIVATE cxx_std_23)
    target_include_directories(test_random PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_random PRIVATE xoshiro)
    add_test(NAME test_random COMMAND test_random)

    # C++17 测试（XoshiroCpp.hpp）
    add_executable(test_xoshirocpp test_xoshirocpp.cpp)
    target_compile_features(test_xoshirocpp PRIVATE cxx_std_17)
    target_include_directories(test_xoshirocpp PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_xoshirocpp PRIVATE xoshiro)
    add_test(NAME test_xoshirocpp COMMAND test_xoshirocpp)

    if(XOSHIRO_ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_compile_options(test_random PRIVATE --coverage -O0 -g)
        target_link_options(test_random PRIVATE --coverage)
        target_compile_options(test_xoshirocpp PRIVATE --coverage -O0 -g)
        target_link_options(test_xoshirocpp PRIVATE --coverage)
    endif()
endif()

if(XOSHIRO_BUILD_BENCHMARK)
    add_executable(benchmark benchmark.cpp)
    target_compile_features(benchmark PRIVATE cxx_std_23)
    target_link_libraries(benchmark PRIVATE xoshiro)
endif()
