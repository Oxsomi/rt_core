# OxC3(Oxsomi core 3), a general framework and toolset for cross-platform applications.
# Copyright (C) 2023 - 2025 Oxsomi / Nielsbishere (Niels Brunekreef)
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see https://github.com/Oxsomi/rt_core/blob/main/LICENSE.
# Be aware that GPL3 requires closed source products to be GPL3 too if released to the public.
# To prevent this a separate license will have to be requested at contact@osomi.net for a premium;
# This is called dual licensing.

import argparse
import os
import subprocess

def main():

	parser = argparse.ArgumentParser(description="Build test apk")

	parser.add_argument("-mode", type=str, default="Release", choices=["Release", "Debug", "RelWithDebInfo", "MinSizeRel"], help="Build mode")
	parser.add_argument("-arch", type=str, default="all", choices=["arm64", "x64", "all"], help="Architecture")
	parser.add_argument("-api", type=int, default=33, help="Android api level (e.g. 33 = Android 13)")
	parser.add_argument("-generator", type=str, help="CMake Generator")
	
	parser.add_argument("-keystore", type=str, help="Keystore location", default=None)
	parser.add_argument("-keystore_password", type=str, help="Keystore password", default=None)
	
	parser.add_argument("--sign", help="Sign apk if built", action="store_true")
	parser.add_argument("--run", help="Run apk if built", action="store_true")
	parser.add_argument("--no_apk", help="Skip building the apk", action="store_true")
	parser.add_argument("--skip_build_core3", help="Run full build, if false, can be used to skip building core3", action="store_true")
	parser.add_argument("--skip_build_self", help="Run full build, if false, can be used to skip building self", action="store_true")

	args = parser.parse_args()

	if args.generator == None:
		args.generator = "MinGW Makefiles" if os.name == "nt" else "Unix Makefiles"

	# Build core3

	if not args.skip_build_core3:

		# Build for host
		
		subprocess.check_output("conan create core3/packages/nvapi -s build_type=Release --build=missing")
		subprocess.check_output("conan create core3/packages/amd_ags -s build_type=Release --build=missing")
		subprocess.check_output("conan create core3/packages/spirv_reflect -s build_type=Release --build=missing")
		subprocess.check_output("conan create core3/packages/dxc -s build_type=Release --build=missing")
		subprocess.check_output("conan create core3/packages/agility_sdk -s build_type=Release --build=missing")

		print("-- Building core3 for packaging")

		subprocess.check_output("conan build core3 -s build_type=Release -o \"&:forceVulkan=False\" -o \"&:dynamicLinkingGraphics=True\" -o \"&:enableSIMD=False\" -o \"&:enableTests=False\" -o \"&:enableOxC3CLI=True\" -o \"&:forceFloatFallback=False\" -o \"&:enableShaderCompiler=True\" -o \"&:cliGraphics=False\" --build=missing")
		subprocess.check_output("conan export-pkg core3 -s build_type=Release -o \"&:forceVulkan=False\" -o \"&:dynamicLinkingGraphics=True\" -o \"&:enableSIMD=False\" -o \"&:enableTests=False\" -o \"&:enableOxC3CLI=True\" -o \"&:forceFloatFallback=False\" -o \"&:enableShaderCompiler=True\" -o \"&:cliGraphics=False\"")

		# Build for self

		cwd = os.getcwd()
		os.chdir("core3")
		subprocess.check_output("python3 build_android.py -mode " + args.mode + " -arch " + args.arch + " -api " + str(args.api) + " -generator \"" + args.generator + "\" --install")
		os.chdir(cwd)

	# Build self and then turn into apk

	if not args.skip_build_self:

		archs = [ "x64", "arm64" ]

		if not args.arch == "all":
			archs = [ args.arch ]

		for arch in archs:

			if arch == "x64":
				archName = "x86_64"
			else:
				archName = "armv8"

			profile = "android_" + archName + "_" + str(args.api) + "_" + args.generator
			outputFolder = "\"build/" + args.mode + "/android/" + arch + "\""
			subprocess.check_output("conan build . -of " + outputFolder + " -o enableSIMD=False -o dynamicLinkingGraphics=False -s build_type=" + args.mode + " --profile \"" + profile + "\" --build=missing")

	if not args.no_apk:
		subprocess.check_output("python3 core3/build_android.py --skip_build -mode " + args.mode + " -arch " + args.arch + " -package net.osomi.rt_core -lib rt_core -name \"Test app\" -version 0.1.0 --apk -api " + str(args.api) + (" --sign" if args.sign else "") + (" --run" if args.run else "") + (" -keystore " + args.keystore if args.keystore else "") + (" -keystore_password " + args.keystore_password if args.keystore_password else ""))
	elif args.run:
		subprocess.check_output("python3 core3/build_android.py --skip_build -mode " + args.mode + " -package net.osomi.rt_core -lib rt_core --run")

if __name__ == "__main__":
	main()