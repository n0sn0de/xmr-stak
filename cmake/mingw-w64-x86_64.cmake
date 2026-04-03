# CMake toolchain file for MinGW-w64 cross-compilation (Linux → Windows x86_64)
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake ..
#
# Produces Windows .exe and .dll binaries from a Linux host.
# Test with Wine: wine64 bin/n0s-ryo-miner.exe --version

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Cross-compiler paths (posix threading model for C++17 <thread>/<mutex>)
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Where the target environment is
# Support both system-installed MinGW and local (dpkg-deb extracted) MinGW
if(DEFINED ENV{MINGW_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH "$ENV{MINGW_SYSROOT}/usr/x86_64-w64-mingw32")
    # Also tell GCC where its own headers/libs are
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --sysroot=$ENV{MINGW_SYSROOT}/usr/x86_64-w64-mingw32")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --sysroot=$ENV{MINGW_SYSROOT}/usr/x86_64-w64-mingw32")
else()
    set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
endif()

# Adjust the default behavior of the FIND_XXX() commands:
# search headers and libraries in the target environment,
# search programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static linking by default for cross-compiled Windows binaries
set(CMAKE_LINK_STATIC ON CACHE BOOL "Link static for Windows cross-compile" FORCE)

# Static link libgcc and libstdc++ so the .exe is self-contained
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# Windows-specific
add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7+
