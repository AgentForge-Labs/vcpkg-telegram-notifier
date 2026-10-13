from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class AgentForgeTelegramNotifierConan(ConanFile):
    name = "agentforge-telegram-notifier"
    version = "0.2.0"
    package_type = "library"
    license = "AGPL-3.0-only"
    url = "https://github.com/AgentForge-Labs/telegram-notifier-public_commercial"
    description = "Official C++20 client for the AgentForge Telegram Notifier public v1 API"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "src/*", "README.md", "LICENSE"
    required_conan_version = ">=2.28"

    def requirements(self):
        self.requires("libcurl/[>=8.5 <9]", transitive_libs=True)
        self.requires("nlohmann_json/[>=3.11 <4]", transitive_headers=True)

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def validate(self):
        check_min_cppstd(self, "20")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        CMakeToolchain(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={"AGENTFORGE_TELEGRAM_NOTIFIER_BUILD_TESTS": False})
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["agentforge_telegram_notifier"]
        self.cpp_info.requires = ["libcurl::libcurl", "nlohmann_json::nlohmann_json"]
        self.cpp_info.set_property("cmake_file_name", "AgentForgeTelegramNotifier")
        self.cpp_info.set_property("cmake_target_name", "agentforge::telegram_notifier")
