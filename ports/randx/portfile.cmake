vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lidaixingchen/RandX
    REF v${VERSION}
    SHA512 1417a75c7ba320df4385af7f065037667ab0b286985f91db5be6d181c39ac26804bddbdcb2a9c7d0d756f8a1c95971d9a46ad7a3899a8bb6b19899f9e63be476
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME RandX CONFIG_PATH lib/cmake/RandX)

# 纯头文件 INTERFACE 库：移除由 CMake 默认生成的空 debug 与 lib 目录
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/randx" RENAME copyright)
