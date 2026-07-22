cmake_minimum_required(VERSION 3.20)
project(RandX VERSION 1.3.1 LANGUAGES CXX)

# 纯头文件库 → INTERFACE target
add_library(RandX INTERFACE)
add_library(RandX::RandX ALIAS RandX)

target_include_directories(RandX INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# v1.3: ChaCha20 CSPRNG 的跨平台 OS 熵源依赖
# Windows: BCryptGenRandom → bcrypt
# macOS:   SecRandomCopyBytes → Security framework
# Linux:   getrandom → libc 内置，无需额外链接
if(WIN32)
    target_link_libraries(RandX INTERFACE bcrypt)
elseif(APPLE)
    find_library(SECRET_LIBRARY Security REQUIRED)
    target_link_libraries(RandX INTERFACE ${SECRET_LIBRARY})
endif()

# 安装
include(GNUInstallDirs)
install(FILES RandX.hpp RandX_Cpp17.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS RandX EXPORT RandXTargets)
install(EXPORT RandXTargets
    NAMESPACE RandX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RandX
)

# 生成 config 文件
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/RandXConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RandXConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RandX
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/RandXConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/RandXConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/RandXConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RandX
)

# 可选：构建测试和 benchmark
option(RANDX_BUILD_TESTS "Build tests" OFF)
option(RANDX_BUILD_BENCHMARK "Build benchmark" OFF)
option(RANDX_ENABLE_COVERAGE "Enable code coverage (GCC/Clang)" OFF)

if(RANDX_BUILD_TESTS)
    enable_testing()

    # 共用 doctest 头文件
    set(DOCTEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

    # C++23 测试（RandX.hpp）
    add_executable(test_randx test_randx.cpp)
    target_compile_features(test_randx PRIVATE cxx_std_23)
    target_include_directories(test_randx PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_randx PRIVATE RandX)
    add_test(NAME test_randx COMMAND test_randx)

    # C++17 测试（RandX_Cpp17.hpp）
    add_executable(test_randx_cpp17 test_randx_cpp17.cpp)
    target_compile_features(test_randx_cpp17 PRIVATE cxx_std_17)
    target_include_directories(test_randx_cpp17 PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_randx_cpp17 PRIVATE RandX)
    add_test(NAME test_randx_cpp17 COMMAND test_randx_cpp17)

    if(RANDX_ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_compile_options(test_randx PRIVATE --coverage -O0 -g)
        target_link_options(test_randx PRIVATE --coverage)
        target_compile_options(test_randx_cpp17 PRIVATE --coverage -O0 -g)
        target_link_options(test_randx_cpp17 PRIVATE --coverage)
    endif()
endif()

if(RANDX_BUILD_BENCHMARK)
    add_executable(benchmark benchmark.cpp)
    target_compile_features(benchmark PRIVATE cxx_std_23)
    target_link_libraries(benchmark PRIVATE RandX)
endif()
