cmake_minimum_required(VERSION 3.20)
project(xoshiro VERSION 2.0.0 LANGUAGES CXX)

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

if(XOSHIRO_BUILD_TESTS)
    enable_testing()

    add_executable(test_random test_random.cpp)
    target_compile_features(test_random PRIVATE cxx_std_23)
    target_link_libraries(test_random PRIVATE xoshiro)
    add_test(NAME test_random COMMAND test_random)

    add_executable(test_xoshirocpp test_xoshirocpp.cpp)
    target_compile_features(test_xoshirocpp PRIVATE cxx_std_17)
    target_link_libraries(test_xoshirocpp PRIVATE xoshiro)
    add_test(NAME test_xoshirocpp COMMAND test_xoshirocpp)
endif()

if(XOSHIRO_BUILD_BENCHMARK)
    add_executable(benchmark benchmark.cpp)
    target_compile_features(benchmark PRIVATE cxx_std_23)
    target_link_libraries(benchmark PRIVATE xoshiro)
endif()
