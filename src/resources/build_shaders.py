#!/usr/bin/python

import os
import subprocess

files = [
	( "overlay.ps", [
		( "PSAA", "ps_3_0", "overlay.cso" ) ] ) ]

outputdir = "..\\..\\public\\openvr\\resources\\"

defaultpath = "C:\\Program Files (x86)\\Microsoft DirectX SDK (June 2010)\\"
sdkpath = os.environ.get('DXSDK_DIR', defaultpath)

def exit_with_error(err):
	print err
	raw_input("\nPress Enter to exit...")
	exit()

if not os.path.isdir(sdkpath):
	exit_with_error("DirectX SDK not found!")

compiler = sdkpath + "Utilities\\bin\\x86\\fxc.exe"

if not os.path.exists(compiler):
	exit_with_error("Shader compiler not found!\n" + compiler)

num_success = 0
num_failure = 0

for input, output in files:
	print "\nCompiling:", input + "..."
	for entry, profile, filename in output:

		print "\n# entry:", entry, "profile:", profile, "output:", filename + "\n"

		target = outputdir + filename
		exists = os.path.exists(target)

		if exists:
			subprocess.call( [ "P4", "edit", target ] )

		error = subprocess.call( [ compiler, "/E", entry, "/T", profile, "/Fo", target, input ] )
		if error == 0:
			num_success += 1
		else:
			num_failure += 1

		if not exists:
			subprocess.call( [ "P4", "add", "-t", "binary", target ] )


print ""
print "***************************************"
print " Don't forget to check in updated files"
print " Succeeded:", num_success, "Failed:", num_failure
print "***************************************"
print ""

raw_input("Press Enter to exit...")

