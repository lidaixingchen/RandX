vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lidaixingchen/Pseudo-random-number-generator-based-on-Xoshiro
    REF v${VERSION}
    SHA512 0  # 发布 tag 后需更新
    HEAD_REF master
)

file(INSTALL "${SOURCE_PATH}/Random.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/XoshiroCpp.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
