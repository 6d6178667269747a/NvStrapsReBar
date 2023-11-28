#!/usr/bin/env python3
#
# Copyright (c) 2022 xCuri0 <zkqri0@gmail.com>
# SPDX-License-Identifier: MIT
#
import os
import sys
import glob
import subprocess
import glob
from pefile import PE

name = "NvStrapsReBar"
version = "1.0"
GUID = "90d10790-bbfa-404b-873b-5bdb3ada3c56"
shell = sys.platform == "win32"
buildtype = "RELEASE"


def filesub(filep, f, r):
    # Read in the file
    with open(filep, 'r') as file :
        filedata = file.read()

    # Replace the target string
    filedata = filedata.replace(f, r)

    # Write the file out again
    with open(filep, 'w') as file:
        file.write(filedata)

def set_bit(data, bit):
    """Sets a specific bit."""
    return data | (1 << bit)

def set_nx_compat_flag(pe):
    """Sets the nx_compat flag to 1 in the PE/COFF file."""
    dllchar = pe.OPTIONAL_HEADER.DllCharacteristics
    dllchar = set_bit(dllchar, 8)  # 8th bit is the nx_compat_flag
    pe.OPTIONAL_HEADER.DllCharacteristics = dllchar
    pe.merge_modified_section_data()
    return pe

if len(sys.argv) > 1:
    buildtype = sys.argv[1].upper()

# 3 arguments = Github Actions
if len(sys.argv) == 3:
    print("TARGET: ", os.environ['TARGET'])
    print("TARGET_ARCH: ", os.environ['TARGET_ARCH'])
    print("TOOL_CHAIN_TAG: ", os.environ['TOOL_CHAIN_TAG'])

    # setup Conf/target.txt
    filesub("./Conf/target.txt", "DEBUG", os.environ['TARGET'])
    filesub("./Conf/target.txt", "IA32", os.environ['TARGET_ARCH'])
    filesub("./Conf/target.txt", "VS2015x86", os.environ['TOOL_CHAIN_TAG'])
else:
    os.chdir("../..")

subprocess.run(["build", "--platform=NvStrapsReBar/ReBarDxe/ReBar.dsc"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

ReBarDXE = glob.glob(f"./Build/NvStrapsReBar/{buildtype}_*/X64/NvStrapsReBar.efi")

if len(ReBarDXE) != 1:
    print("Build failed")
    sys.exit(1)

# set NX_COMPAT
pe = PE(ReBarDXE[0])
set_nx_compat_flag(pe)

os.remove(ReBarDXE[0])
pe.write(ReBarDXE[0])

print(ReBarDXE[0])
print("Building FFS")
os.chdir(os.path.dirname(ReBarDXE[0]))

try:
    os.remove("pe32.sec")
    os.remove("name.sec")
    os.remove("NvStrapsReBar.ffs")
except FileNotFoundError:
    pass

subprocess.run(["GenSec", "-o", "pe32.sec", "NvStrapsReBar.efi", "-S", "EFI_SECTION_PE32"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)
subprocess.run(["GenSec", "-o", "name.sec", "-S", "EFI_SECTION_USER_INTERFACE", "-n", name], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)
subprocess.run(["GenFfs", "-g", GUID, "-o", "NvStrapsReBar.ffs", "-i", "pe32.sec", "-i" ,"name.sec", "-t", "EFI_FV_FILETYPE_DRIVER", "--checksum"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

try:
    os.remove("pe32.sec")
    os.remove("name.sec")
except FileNotFoundError:
    pass

print("Finished")