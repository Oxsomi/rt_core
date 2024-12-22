from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.scm import Git
from conan.tools.files import collect_libs, copy
import os

required_conan_version = ">=2.0"

class rt_core(ConanFile):

	name = "rt_core"
	version = "0.1.0"

	# Optional metadata
	license = "GPLv3 and dual licensable"
	author = "Oxsomi / Nielsbishere"
	url = "https://github.com/Oxsomi/rt_core"
	description = "Layer on OxC3 that handles raytracing abstractions"
	# TODO: topics = ("")

	# Binary configuration
	settings = "os", "compiler", "build_type", "arch"

	options = {
		"forceVulkan": [ True, False ],
		"enableSIMD": [ True, False ],
		"dynamicLinkingGraphics": [ True, False ]
	}

	default_options = {
		"forceVulkan": False,
		"enableSIMD": True,
		"dynamicLinkingGraphics": False
	}

	exports_sources = [ "inc/*", "cmake/*" ]

	def layout(self):
		cmake_layout(self)

	def configure(self):
		self.settings.rm_safe("compiler.cppstd")
		self.settings.rm_safe("compiler.libcxx")

	def generate(self):

		deps = CMakeDeps(self)
		deps.generate()

		tc = CMakeToolchain(self)
		tc.cache_variables["CMAKE_CONFIGURATION_TYPES"] = str(self.settings.build_type)
		tc.generate()

	def source(self):
		git = Git(self)
		git.clone(url=self.conan_data["sources"][self.version]["url"])
		git.folder = os.path.join(self.source_folder, "core3")
		git.checkout(self.conan_data["sources"][self.version]["checkout"])
		git.run("submodule update --init --recursive")

	def build(self):

		cmake = CMake(self)

		if os.path.isdir("rt_core"):
			cmake.configure(build_script_folder="rt_core")
		else:
			cmake.configure()

		cmake.build()

	def build_requirements(self):
		self.tool_requires("oxc3/0.2.078", options = {
			"forceVulkan": self.options.forceVulkan,
			"enableSIMD": self.options.enableSIMD,
			"enableTests": False,
			"enableOxC3CLI": True,
			"forceFloatFallback": False,
			"enableShaderCompiler": True,
			"cliGraphics": False,
			"dynamicLinkingGraphics": True
		})

	def requirements(self):
		self.requires("oxc3/0.2.078", options = {
			"forceVulkan": self.options.forceVulkan,
			"enableSIMD": self.options.enableSIMD,
			"enableTests": False,
			"enableOxC3CLI": False,
			"forceFloatFallback": False,
			"enableShaderCompiler": False,
			"cliGraphics": False,
			"dynamicLinkingGraphics": self.options.dynamicLinkingGraphics
		})

	def package(self):
		cmake = CMake(self)
		cmake.build(target="rt_core")

		inc_src = os.path.join(self.source_folder, "inc")
		inc_dst = os.path.join(self.package_folder, "inc")
		copy(self, "*.h", inc_src, inc_dst)
		copy(self, "*.hpp", inc_src, inc_dst)

		cwd = os.getcwd()

		lib_dst = os.path.join(self.package_folder, "lib")
		bin_dst = os.path.join(self.package_folder, "bin")

		# Linux, OSX, etc. all run from build/Debug or build/Release, so we need to change it a bit
		if cwd.endswith("Debug") or cwd.endswith("Release"):
			copy(self, "*.a", os.path.join(self.build_folder, "lib"), os.path.join(self.package_folder, "lib"))
			copy(self, "^([^.]+)$", os.path.join(self.build_folder, "bin"), os.path.join(self.package_folder, "bin"))

		# Windows uses more complicated setups
		else:
			dbg_lib_src = os.path.join(self.build_folder, "lib/Debug")
			dbg_bin_src = os.path.join(self.build_folder, "bin/Debug")

			copy(self, "*.lib", dbg_lib_src, lib_dst)
			copy(self, "*.pdb", dbg_lib_src, lib_dst)
			copy(self, "*.exp", dbg_lib_src, lib_dst)

			copy(self, "*.exp", dbg_bin_src, bin_dst)
			copy(self, "*.exe", dbg_bin_src, bin_dst)
			copy(self, "*.dll", dbg_bin_src, bin_dst)
			copy(self, "*.pdb", dbg_bin_src, bin_dst)

			if os.path.isfile(lib_dst):
				for filename in os.listdir(lib_dst):
					f = os.path.join(lib_dst, filename)
					if os.path.isfile(f):
						offset = f.rfind(".")
						rename(self, f, f[:offset] + "d." + f[offset+1:])

			if os.path.isfile(bin_dst):
				for filename in os.listdir(bin_dst):
					f = os.path.join(bin_dst, filename)
					if os.path.isfile(f):
						offset = f.rfind(".")
						rename(self, f, f[:offset] + "d." + f[offset+1:])

			# Copy release libs

			rel_lib_src = os.path.join(self.build_folder, "Release/lib")
			rel_bin_src = os.path.join(self.build_folder, "Release/bin")

			copy(self, "*.lib", rel_lib_src, lib_dst)
			copy(self, "*.pdb", rel_lib_src, lib_dst)
			copy(self, "*.exp", rel_lib_src, lib_dst)

			copy(self, "*.exp", rel_bin_src, bin_dst)
			copy(self, "*.exe", rel_bin_src, bin_dst)
			copy(self, "*.dll", rel_bin_src, bin_dst)
			copy(self, "*.pdb", rel_bin_src, bin_dst)

	def package_info(self):
		self.cpp_info.components["rt_core"].libs = ["rt_core"]
		self.cpp_info.set_property("cmake_file_name", "rt_core")
		self.cpp_info.set_property("cmake_target_name", "rt_core")
		self.cpp_info.set_property("pkg_config_name", "rt_core")
		self.cpp_info.libs = collect_libs(self)
