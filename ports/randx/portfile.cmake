vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lidaixingchen/RandX
    REF v${VERSION}
    SHA512 3f69fa97f63bedaf646b61b93e7ec9002c69783ad5696e21d73eb538c5968d6bfdbd4135c0a3129e278cd390f460ab753a0dfc1befda2256f78c784c82af1d72
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME RandX CONFIG_PATH lib/cmake/RandX)

# 纯头文件 INTERFACE 库：移除由 CMake 默认生成的空 debug 与 lib 目录
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/randx" RENAME copyright)
