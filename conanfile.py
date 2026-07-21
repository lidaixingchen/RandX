from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
import os


class RandXConan(ConanFile):
    name = "randx"
    version = "1.3.0"
    description = "Modern, fast, and header-only C++ pseudo-random number generator and distribution library"
    homepage = "https://github.com/lidaixingchen/RandX"
    url = homepage
    license = "MIT"
    author = "lidaixingchen"
    topics = ("random", "prng", "randx", "sfc64", "xoshiro", "chacha20", "csprng", "header-only", "constexpr")
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "CMakeLists.txt", "RandX.hpp", "RandX_Cpp17.hpp", "cmake/*"
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
        self.cpp_info.set_property("cmake_file_name", "RandX")
        self.cpp_info.set_property("cmake_target_name", "RandX::RandX")

    def package_id(self):
        self.info.clear()  # header-only: binary-independent
