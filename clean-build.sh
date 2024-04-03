#! /bin/sh
# Clean old build files, then build the executable.

rm -rf CMakeCache.txt CMakeFiles Makefile cmake-build-debug cmake_install.cmake traffix_linux
cmake . && cmake --build . --target traffix_linux
