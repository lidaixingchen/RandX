from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
import os


class XoshiroCppConan(ConanFile):
    name = "xoshiro-cpp"
    version = "2.0.0"
    description = "Header-only C++ PRNG library based on xoshiro/xoroshiro algorithms"
    homepage = "https://github.com/lidaixingchen/Pseudo-random-number-generator-based-on-Xoshiro"
    url = homepage
    license = "MIT"
    author = "lidaixingchen"
    topics = ("random", "prng", "xoshiro", "xoroshiro", "header-only", "constexpr")
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "CMakeLists.txt", "Random.hpp", "XoshiroCpp.hpp", "cmake/*"
    no_copy_source = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        pass  # header-only, nothing to build

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "xoshiro")
        self.cpp_info.set_property("cmake_target_name", "xoshiro::xoshiro")

    def package_id(self):
        self.info.clear()  # header-only: binary-independent
