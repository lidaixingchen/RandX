vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lidaixingchen/Pseudo-random-number-generator-based-on-Xoshiro
    REF v${VERSION}
    SHA512 260dff775e6d70fdc69fd805314c143c18bf70675c38f18f80bd3c247bb2e6b4500051d1c9000db4f0db0cea260d5a24bfd9bec06fcc3a268d117237dfcf848f
    HEAD_REF master
)

file(INSTALL "${SOURCE_PATH}/Random.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/XoshiroCpp.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
