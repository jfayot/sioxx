import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy

required_conan_version = ">=2.28"


class SioxxConan(ConanFile):
    name = "sioxx"
    version = "0.2.0"
    package_type = "library"

    license = "MIT"
    url = "https://github.com/jfayot/sioxx"
    homepage = "https://github.com/jfayot/sioxx"
    description = "A modern C++17 Socket.IO client built on Boost.Beast"
    topics = ("socket.io", "websocket", "boost-beast", "networking")

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "boost/*:header_only": True,
    }

    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "cmake/*",
        "include/*",
        "src/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def requirements(self):
        self.requires(
            "boost/1.90.0",
            transitive_headers=False,
            transitive_libs=False,
            visible=False,
        )
        self.requires("nlohmann_json/3.12.0", transitive_headers=True)
        self.requires(
            "openssl/3.6.3",
            transitive_headers=False,
            transitive_libs=not self.options.shared,
            visible=not self.options.shared,
        )

    def validate(self):
        check_min_cppstd(self, 17)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        dependencies = CMakeDeps(self)
        dependencies.generate()

        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = bool(self.options.shared)
        toolchain.variables["SIOXX_BUILD_DOCS"] = False
        toolchain.variables["SIOXX_BUILD_EXAMPLES"] = False
        toolchain.variables["SIOXX_BUILD_TESTS"] = False
        toolchain.variables["SIOXX_INSTALL"] = True
        toolchain.variables["SIOXX_USE_SYSTEM_BOOST"] = True
        toolchain.variables["SIOXX_USE_SYSTEM_JSON"] = True
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.libs = ["sioxx"]
        self.cpp_info.set_property("cmake_file_name", "sioxx")
        self.cpp_info.set_property("cmake_target_name", "sioxx::sioxx")
        self.cpp_info.requires = ["nlohmann_json::nlohmann_json"]

        if not self.options.shared:
            self.cpp_info.requires.extend(["openssl::ssl", "openssl::crypto"])
            if self.settings.os == "Windows":
                self.cpp_info.system_libs.extend(["ws2_32", "mswsock"])
            elif self.settings.os in ("Linux", "FreeBSD"):
                self.cpp_info.system_libs.append("pthread")
