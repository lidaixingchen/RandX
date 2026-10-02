cmake_minimum_required(VERSION 3.20)
project(RandX VERSION 1.5.0 LANGUAGES CXX)

include(GNUInstallDirs)

# 纯头文件库 → INTERFACE target
add_library(RandX INTERFACE)
add_library(RandX::RandX ALIAS RandX)
target_compile_features(RandX INTERFACE cxx_std_17)
target_include_directories(RandX INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(RandX_Cpp17 INTERFACE)
add_library(RandX::Cpp17 ALIAS RandX_Cpp17)
add_library(RandX::RandX_Cpp17 ALIAS RandX_Cpp17)
set_target_properties(RandX_Cpp17 PROPERTIES EXPORT_NAME Cpp17)
target_compile_features(RandX_Cpp17 INTERFACE cxx_std_17)
target_include_directories(RandX_Cpp17 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(RandX_Cpp23 INTERFACE)
add_library(RandX::Cpp23 ALIAS RandX_Cpp23)
add_library(RandX::RandX_Cpp23 ALIAS RandX_Cpp23)
set_target_properties(RandX_Cpp23 PROPERTIES EXPORT_NAME Cpp23)
target_compile_features(RandX_Cpp23 INTERFACE cxx_std_23)
target_include_directories(RandX_Cpp23 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# v1.3: ChaCha20 CSPRNG 的跨平台 OS 熵源依赖
# Windows: BCryptGenRandom → bcrypt
# macOS:   SecRandomCopyBytes → Security framework
# Linux:   getrandom → libc 内置，无需额外链接
if(WIN32)
    target_link_libraries(RandX INTERFACE bcrypt)
    target_link_libraries(RandX_Cpp17 INTERFACE bcrypt)
    target_link_libraries(RandX_Cpp23 INTERFACE bcrypt)
elseif(APPLE)
    target_link_libraries(RandX INTERFACE "-framework Security")
    target_link_libraries(RandX_Cpp17 INTERFACE "-framework Security")
    target_link_libraries(RandX_Cpp23 INTERFACE "-framework Security")
endif()

# 安装
install(FILES RandX.hpp RandX_Cpp17.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS RandX RandX_Cpp17 RandX_Cpp23 EXPORT RandXTargets)
install(EXPORT RandXTargets
    NAMESPACE RandX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RandX
)

# 导出构建树目标，支持下游工程通过 build 目录直接 find_package(RandX CONFIG)
export(EXPORT RandXTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/RandXTargets.cmake"
    NAMESPACE RandX::
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
    ARCH_INDEPENDENT
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/RandXConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/RandXConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RandX
)

# 可选：构建测试、benchmark 和示例
option(RANDX_BUILD_TESTS "Build tests" OFF)
option(RANDX_BUILD_BENCHMARK "Build benchmark" OFF)
option(RANDX_BUILD_EXAMPLES "Build examples" OFF)
option(RANDX_ENABLE_COVERAGE "Enable code coverage (GCC/Clang)" OFF)
option(RANDX_ENABLE_HEADER_TOOLS "启用头文件生成维护目标" OFF)
set(RANDX_SAMPLING_HEADER_DIR "" CACHE PATH "Optional header directory used by benchmark targets")

if(RANDX_ENABLE_HEADER_TOOLS)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)

    add_custom_target(randx_generate_headers
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_headers.py" --write
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "生成 RandX 头文件"
        VERBATIM
    )

    add_custom_target(randx_check_headers
        COMMAND "${Python3_EXECUTABLE}" -X utf8 -m unittest discover
                -s tools -p "test_*header*.py" -v
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_headers.py" --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "检查 RandX 头文件生成结果"
        VERBATIM
    )
endif()

if(RANDX_BUILD_TESTS)
    enable_testing()

    # MSVC 的 C++23 测试限定到该标准的预览模式。
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++23preview")
        set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++23preview")
    endif()

    # 共用 doctest 头文件
    set(DOCTEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

    # C++23 测试（RandX.hpp）
    add_executable(test_randx test_randx.cpp)
    set_target_properties(test_randx PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_features(test_randx PRIVATE cxx_std_23)
    target_include_directories(test_randx PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_randx PRIVATE RandX_Cpp23)
    add_test(NAME test_randx COMMAND test_randx)

    # C++17 测试（RandX_Cpp17.hpp）
    add_executable(test_randx_cpp17 test_randx_cpp17.cpp)
    set_target_properties(test_randx_cpp17 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_features(test_randx_cpp17 PRIVATE cxx_std_17)
    target_include_directories(test_randx_cpp17 PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_link_libraries(test_randx_cpp17 PRIVATE RandX_Cpp17)
    add_test(NAME test_randx_cpp17 COMMAND test_randx_cpp17)

    find_package(Threads REQUIRED)

    # 故障注入目标在所有翻译单元中统一启用私有熵 hook。
    add_executable(
        test_entropy_cpp17
        tests/entropy/test_entropy_cpp17.cpp
        tests/entropy/entropy_cross_translation_unit.cpp
    )
    set_target_properties(test_entropy_cpp17 PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_features(test_entropy_cpp17 PRIVATE cxx_std_17)
    target_include_directories(test_entropy_cpp17 PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_compile_definitions(test_entropy_cpp17 PRIVATE
        RANDX_ENABLE_ENTROPY_TEST_HOOKS
        RANDX_ENTROPY_TEST_CPP17
    )
    target_link_libraries(test_entropy_cpp17 PRIVATE RandX_Cpp17 Threads::Threads)
    add_test(NAME test_entropy_cpp17 COMMAND test_entropy_cpp17)

    add_executable(
        test_entropy_cpp23
        tests/entropy/test_entropy_cpp23.cpp
        tests/entropy/entropy_cross_translation_unit.cpp
    )
    set_target_properties(test_entropy_cpp23 PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_features(test_entropy_cpp23 PRIVATE cxx_std_23)
    target_include_directories(test_entropy_cpp23 PRIVATE ${DOCTEST_INCLUDE_DIR})
    target_compile_definitions(test_entropy_cpp23 PRIVATE RANDX_ENABLE_ENTROPY_TEST_HOOKS)
    target_link_libraries(test_entropy_cpp23 PRIVATE RandX_Cpp23 Threads::Threads)
    add_test(NAME test_entropy_cpp23 COMMAND test_entropy_cpp23)

    # 套件名采用 UTF-8，供注册清单工具跨平台读取。
    if(MSVC)
        target_compile_options(test_randx PRIVATE /utf-8)
        target_compile_options(test_randx_cpp17 PRIVATE /utf-8)
        target_compile_options(test_entropy_cpp17 PRIVATE /utf-8)
        target_compile_options(test_entropy_cpp23 PRIVATE /utf-8)
    endif()

    # 固定 doctest 用例排序种子，仅用于重排测试，不用于 RandX 引擎播种。
    set(RANDX_DOCTEST_ORDER_SEED "20261001")
    add_test(
        NAME test_randx_randomized_order
        COMMAND test_randx --order-by=rand --rand-seed=${RANDX_DOCTEST_ORDER_SEED}
    )
    add_test(
        NAME test_randx_cpp17_randomized_order
        COMMAND test_randx_cpp17 --order-by=rand --rand-seed=${RANDX_DOCTEST_ORDER_SEED}
    )
    add_test(
        NAME test_entropy_cpp17_randomized_order
        COMMAND test_entropy_cpp17 --order-by=rand --rand-seed=${RANDX_DOCTEST_ORDER_SEED}
    )
    add_test(
        NAME test_entropy_cpp23_randomized_order
        COMMAND test_entropy_cpp23 --order-by=rand --rand-seed=${RANDX_DOCTEST_ORDER_SEED}
    )

    # Python 标准库工具直接检查现有两个目标，不额外构建注册清单程序。
    find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME test_contract_registry
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_test_contracts.py"
                    compare-binaries
                    --cpp17-binary "$<TARGET_FILE:test_randx_cpp17>"
                    --cpp23-binary "$<TARGET_FILE:test_randx>"
        )
        add_test(
            NAME test_entropy_contract_registry
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_test_contracts.py"
                    compare-binaries
                    --cpp17-binary "$<TARGET_FILE:test_entropy_cpp17>"
                    --cpp23-binary "$<TARGET_FILE:test_entropy_cpp23>"
                    --suite "公共/基础/安全熵源故障"
                    --suite "内部/熵源"
        )
    endif()

    if(RANDX_ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_compile_options(test_randx PRIVATE --coverage -O0 -g)
        target_link_options(test_randx PRIVATE --coverage)
        target_compile_options(test_randx_cpp17 PRIVATE --coverage -O0 -g)
        target_link_options(test_randx_cpp17 PRIVATE --coverage)
        target_compile_options(test_entropy_cpp17 PRIVATE --coverage -O0 -g)
        target_link_options(test_entropy_cpp17 PRIVATE --coverage)
        target_compile_options(test_entropy_cpp23 PRIVATE --coverage -O0 -g)
        target_link_options(test_entropy_cpp23 PRIVATE --coverage)
    endif()
endif()

if(RANDX_BUILD_BENCHMARK)
    # 零依赖快速检查（保留不变）
    # 目标名 benchmark_quick：避免与 Google Benchmark 的 benchmark 库目标冲突
    add_executable(benchmark_quick benchmark.cpp)
    target_compile_features(benchmark_quick PRIVATE cxx_std_23)
    target_link_libraries(benchmark_quick PRIVATE RandX)

    # Google Benchmark 正式套件
    include(FetchContent)
    FetchContent_Declare(
        benchmark_gbench_dep
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.5
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(benchmark_gbench_dep)

    add_executable(benchmark_gbench benchmark_gbench.cpp)
    target_link_libraries(benchmark_gbench PRIVATE benchmark::benchmark RandX)
    target_compile_features(benchmark_gbench PRIVATE cxx_std_23)

    function(randx_set_benchmark_header_override target_name)
        if(RANDX_SAMPLING_HEADER_DIR)
            target_include_directories(${target_name} BEFORE PRIVATE "${RANDX_SAMPLING_HEADER_DIR}")
        endif()
    endfunction()
    randx_set_benchmark_header_override(benchmark_gbench)

    function(randx_add_sampling_benchmark target_name standard library_target)
        add_executable(${target_name} benchmark_gbench.cpp)
        target_link_libraries(${target_name} PRIVATE benchmark::benchmark ${library_target})
        target_compile_features(${target_name} PRIVATE cxx_std_${standard})
        target_compile_definitions(${target_name} PRIVATE RANDX_BENCHMARK_SAMPLING_ONLY)
        set_target_properties(${target_name} PROPERTIES
            CXX_STANDARD ${standard}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        randx_set_benchmark_header_override(${target_name})
    endfunction()

    # 两个目标使用相同基准源分别编译公共头文件。
    randx_add_sampling_benchmark(benchmark_gbench_sampling_cpp17 17 RandX_Cpp17)
    target_compile_definitions(benchmark_gbench_sampling_cpp17 PRIVATE RANDX_BENCHMARK_CPP17)
    randx_add_sampling_benchmark(benchmark_gbench_sampling_cpp23 23 RandX_Cpp23)
endif()

if(RANDX_BUILD_EXAMPLES)
    # examples/ 全部示例 + PractRand 序列生成器（均基于 RandX.hpp，需 C++23）
    set(RANDX_EXAMPLE_SOURCES
        examples/chacha20_csprng.cpp
        examples/compile_time.cpp
        examples/engine_management.cpp
        examples/multi_stream.cpp
        examples/weighted_select.cpp
        gen_sequences.cpp
    )
    foreach(example_source IN LISTS RANDX_EXAMPLE_SOURCES)
        get_filename_component(example_name ${example_source} NAME_WE)
        add_executable(example_${example_name} ${example_source})
        target_compile_features(example_${example_name} PRIVATE cxx_std_23)
        target_link_libraries(example_${example_name} PRIVATE RandX)
    endforeach()

    # 下游消费者工程构建验证（基于 find_package(RandX CONFIG)）
    set(CONSUMER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/consumer_build")
    set(CONSUMER_CMAKE_ARGS
        "-DCMAKE_PREFIX_PATH=${CMAKE_CURRENT_BINARY_DIR}"
    )
    if(CMAKE_CXX_COMPILER)
        list(APPEND CONSUMER_CMAKE_ARGS "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
    if(CMAKE_BUILD_TYPE)
        list(APPEND CONSUMER_CMAKE_ARGS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND CONSUMER_CMAKE_ARGS "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND CONSUMER_CMAKE_ARGS "-A" "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND CONSUMER_CMAKE_ARGS "-T" "${CMAKE_GENERATOR_TOOLSET}")
    endif()

    add_custom_target(consumer_validation ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CONSUMER_BUILD_DIR}"
        COMMAND ${CMAKE_COMMAND} -B "${CONSUMER_BUILD_DIR}"
                                 -S "${CMAKE_CURRENT_SOURCE_DIR}/examples/consumer_validation"
                                 -G "${CMAKE_GENERATOR}"
                                 ${CONSUMER_CMAKE_ARGS}
        COMMAND ${CMAKE_COMMAND} --build "${CONSUMER_BUILD_DIR}" $<$<BOOL:$<CONFIG>>:--config> $<$<BOOL:$<CONFIG>>:$<CONFIG>>
        COMMENT "构建下游消费者工程验证用例 (find_package(RandX CONFIG))"
    )
endif()
