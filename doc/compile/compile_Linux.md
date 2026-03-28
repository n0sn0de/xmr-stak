# Compile n0s-cngpu for Linux

## Dependencies

### Required

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake libmicrohttpd-dev libssl-dev libhwloc-dev
```

### AMD OpenCL (optional — for AMD GPU mining)

```bash
sudo apt install ocl-icd-opencl-dev opencl-headers
```

You also need the AMD GPU driver installed:
- **AMDGPU-PRO:** `./amdgpu-pro-install --opencl=legacy,pal`
- **ROCm:** See [ROCm install guide](https://rocm.docs.amd.com/en/latest/deploy/linux/install.html)

### NVIDIA CUDA (optional — for NVIDIA GPU mining)

Download and install the [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads).

For minimal install, select:
- CUDA / Development
- CUDA / Runtime
- Driver components

## Build

### CPU-only (simplest)

```bash
git clone https://github.com/n0sn0de/xmr-stak.git n0s-cngpu
cd n0s-cngpu
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=OFF
make -j$(nproc)
```

### With AMD/OpenCL

```bash
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON
make -j$(nproc)
```

### With NVIDIA/CUDA

```bash
mkdir build && cd build
cmake .. -DCUDA_ENABLE=ON -DOpenCL_ENABLE=OFF
make -j$(nproc)
```

### With both GPU backends

```bash
mkdir build && cd build
cmake .. -DCUDA_ENABLE=ON -DOpenCL_ENABLE=ON
make -j$(nproc)
```

### Release build (portable)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DXMR-STAK_COMPILE=generic
make -j$(nproc)
```

Or use the packaging script:

```bash
./scripts/package-release.sh
```

## Install location

After building, the binary is at `build/bin/n0s-cngpu`.

To install to a custom prefix:

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/n0s-cngpu
make install
```

## CMake Options

| Option | Default | Description |
|---|---|---|
| `CUDA_ENABLE` | ON | NVIDIA CUDA backend |
| `OpenCL_ENABLE` | ON | AMD OpenCL backend |
| `MICROHTTPD_ENABLE` | ON | HTTP monitoring API |
| `OpenSSL_ENABLE` | ON | TLS pool connections |
| `CMAKE_BUILD_TYPE` | Release | Build type |
| `XMR-STAK_COMPILE` | native | CPU optimization (native/generic) |
| `CMAKE_LINK_STATIC` | OFF | Static link libgcc/libstdc++ |

## Supported Ubuntu Versions

| Version | Codename | GCC | CMake | Notes |
|---|---|---|---|---|
| 18.04 | Bionic | 7 | 3.10 | Minimum supported |
| 20.04 | Focal | 9 | 3.16 | |
| 22.04 | Jammy | 11 | 3.22 | |
| 24.04 | Noble | 13 | 3.28 | Recommended |

Other distributions with GCC 7+ and CMake 3.10+ should work.

## Troubleshooting

### `internal compiler error: Killed`

Not enough RAM. Need at least 1 GB free. Try reducing parallel jobs: `make -j1`

### OpenCL not found

Make sure `ocl-icd-opencl-dev` and `opencl-headers` are installed, and your GPU driver provides an ICD.

### CUDA not found

Ensure the CUDA toolkit is in your PATH:

```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```
