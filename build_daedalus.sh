#!/bin/bash

PROC_NR=$(getconf _NPROCESSORS_ONLN)

## This file is the standard way of building Daedalus for the PSP.
## Usage: ./build_daedalus.sh [DEBUG]
## The PSP argument from older instructions is still accepted and ignored.

#Clear last build
rm -rf build

function psp_plugins() {
  make --quiet -j $PROC_NR -C "$PWD/Source/SysPSP/PRX/DveMgr" || { exit 1; }
  make --quiet -j $PROC_NR -C "$PWD/Source/SysPSP/PRX/ExceptionHandler" || { exit 1; }
  make --quiet -j $PROC_NR -C "$PWD/Source/SysPSP/PRX/MediaEngine" || { exit 1; }
}

CMAKEDEFINES=""
if [[ $1 = "DEBUG" ]] || [[ $2 = "DEBUG" ]]; then
    CMAKEDEFINES+=" -DCMAKE_BUILD_TYPE=Debug -DDEBUG=1 "
else
    CMAKEDEFINES+=" -DCMAKE_BUILD_TYPE=Release"
fi

psp_plugins
psp-cmake $CMAKEDEFINES -S . -B build || { exit 1; }
cmake --build build -j${PROC_NR} || { exit 1; }
cmake --install build --prefix $PWD
