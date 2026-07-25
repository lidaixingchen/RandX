vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lidaixingchen/RandX
    REF v${VERSION}
    SHA512 e2063d17313b88b06e3ee8b5550da4a5cafcb57790a2bcd3290673828cd65ef8cf97b2cbb4525d88c29e2f29f17283afc9b2af93541350fd569e9c331b1c023a
    HEAD_REF master
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME RandX CONFIG_PATH lib/cmake/RandX)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
