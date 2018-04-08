#!/bin/bash

# Continuous Integration script for ccache
# Author: Peter Budai <peterbudai@hotmail.com>

# it is supposed to be run by appveyor-ci

# Enable colors
normal=$(tput sgr0)
red=$(tput setaf 1)
green=$(tput setaf 2)
cyan=$(tput setaf 6)

# Basic status function
_status() {
    local type="${1}"
    local status="${package:+${package}: }${2}"
    local items=("${@:3}")
    case "${type}" in
        failure) local -n nameref_color='red';   title='[CCACHE CI] FAILURE:' ;;
        success) local -n nameref_color='green'; title='[CCACHE CI] SUCCESS:' ;;
        message) local -n nameref_color='cyan';  title='[CCACHE CI]'
    esac
    printf "\n${nameref_color}${title}${normal} ${status}\n\n"
}

# Run command with status
execute(){
    local status="${1}"
    local command="${2}"
    local arguments=("${@:3}")
    cd "${package:-.}"
    message "${status}"
    if [[ "${command}" != *:* ]]
        then ${command} ${arguments[@]}
        else ${command%%:*} | ${command#*:} ${arguments[@]}
    fi || failure "${status} failed"
    cd - > /dev/null
}

# Build
build_ccache() {
    cd $(cygpath ${APPVEYOR_BUILD_FOLDER})
    ./autogen.sh
    mkdir build && cd build

    ../${_realname}/configure

    make
}
# Test
test_ccache() {
    cd $(cygpath ${APPVEYOR_BUILD_FOLDER})
    cd build

    # Not working YET
    make test
}
# Status functions
failure() { local status="${1}"; local items=("${@:2}"); _status failure "${status}." "${items[@]}"; exit 1; }
success() { local status="${1}"; local items=("${@:2}"); _status success "${status}." "${items[@]}"; exit 0; }
message() { local status="${1}"; local items=("${@:2}"); _status message "${status}"  "${items[@]}"; }

# Install build environment and build
PATH=/c/msys64/%MSYSTEM%/bin:$PATH
if [ "$MSYSTEM" == "MSYS" ]; then
  execute 'Installing base-devel and toolchain'  pacman -S --needed --noconfirm msys2-devel
  execute 'Installing dependencies' pacman -S --needed --noconfirm  zlib
else
  execute 'Installing base-devel and toolchain'  pacman -S --needed --noconfirm mingw-w64-$MSYS2_ARCH-toolchain
  execute 'Installing dependencies' pacman -S --needed --noconfirm  mingw-w64-$MSYS2_ARCH-zlib
fi
execute 'Building ccache' build_ccache
execute 'Testing ccache' test_ccache
