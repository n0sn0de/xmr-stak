# Compile n0s-cngpu

## Build System

The build system is [CMake](https://cmake.org/runningcmake/).

Set options on the command line:
- Enable: `cmake .. -DOPTION_NAME=ON`
- Disable: `cmake .. -DOPTION_NAME=OFF`
- Set value: `cmake .. -DOPTION_NAME=value`

Or use the ncurses GUI: `ccmake ..`

## Build Guide

- [Compile on Linux](compile_Linux.md) — Ubuntu/Debian and other distros

## Build Options

| Option | Default | Description |
|---|---|---|
| `CUDA_ENABLE` | ON | NVIDIA CUDA backend |
| `OpenCL_ENABLE` | ON | AMD OpenCL backend |
| `MICROHTTPD_ENABLE` | ON | HTTP monitoring API |
| `OpenSSL_ENABLE` | ON | TLS pool connections |
| `CMAKE_BUILD_TYPE` | Release | Build type (Release/Debug) |
| `XMR-STAK_COMPILE` | native | CPU tuning (native/generic) |
| `CMAKE_LINK_STATIC` | OFF | Static link libgcc/libstdc++ |
| `CMAKE_INSTALL_PREFIX` | /usr/local | Install prefix for `make install` |
