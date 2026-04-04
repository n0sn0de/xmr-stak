# n0s-ryo-miner ⚡

GPU miner for [RYO Currency](https://ryo-currency.com) using the **CryptoNight-GPU** algorithm.

Supports **AMD** (OpenCL) and **NVIDIA** (CUDA) GPUs. No CPU mining — this is a GPU-only miner.

Fork of [xmr-stak](https://github.com/fireice-uk/xmr-stak) by fireice-uk and psychocrypt, stripped down and modernized for CryptoNight-GPU exclusively.

**Latest Release:** [v3.1.0 - GPU Autotune](https://github.com/n0sn0de/n0s-ryo-miner/releases/tag/v3.1.0) 🚀

---

## Table of Contents

- [Download Pre-Built Binaries](#download-pre-built-binaries)
- [Supported Hardware](#supported-hardware)
- [Building from Source](#building-from-source)
  - [Quick Start (Experienced Users)](#quick-start-experienced-users)
  - [Ubuntu/Linux — Detailed Build Guide](#ubuntulinux--detailed-build-guide)
  - [Container Builds (Docker/Podman)](#container-builds-dockerpodman)
  - [Windows — Native MSVC Build](#windows--native-msvc-build)
  - [Windows — MinGW Cross-Compile (from Linux)](#windows--mingw-cross-compile-from-linux)
  - [Build Options Reference](#build-options-reference)
- [Usage](#usage)
- [Benchmarks](#benchmarks)
- [Testing](#testing)
- [Project Structure](#project-structure)
- [Algorithm](#algorithm)
- [License](#license)
- [Credits](#credits)

---

## Download Pre-Built Binaries

Grab the latest release from [GitHub Releases](https://github.com/n0sn0de/n0s-ryo-miner/releases):

| Platform | Binary | Architectures |
|---|---|---|
| **OpenCL (AMD)** | `n0s-ryo-miner-v3.1.0-opencl-ubuntu22.04` | GCN/RDNA/CDNA |
| **CUDA 11.8** | `n0s-ryo-miner-v3.1.0-cuda11.8` | Pascal→Ada (sm_61-89) |
| **CUDA 12.6** | `n0s-ryo-miner-v3.1.0-cuda12.6` | Pascal→Hopper (sm_61-90) |
| **CUDA 12.8** | `n0s-ryo-miner-v3.1.0-cuda12.8` | Pascal→Blackwell (sm_61-120) |
| **Windows (OpenCL)** | `n0s-ryo-miner-v3.1.0-windows-opencl.exe` | Any OpenCL GPU |

> **Note:** Linux OpenCL builds include a separate backend library (`.so`). Place it in the same directory as the binary.

---

## Supported Hardware

### NVIDIA (CUDA)

Minimum: Pascal architecture (GTX 10xx series). Requires CUDA 11.0+.

| Architecture | Compute | Example GPUs | Min CUDA |
|---|---|---|---|
| Pascal | 6.0, 6.1 | GTX 1060/1070/1080, P100 | 11.0 |
| Turing | 7.5 | RTX 2060/2070/2080, T4 | 11.0 |
| Ampere | 8.0, 8.6 | RTX 3060/3070/3080/3090, A100 | 11.0 |
| Ada Lovelace | 8.9 | RTX 4060/4070/4080/4090 | 11.8 |
| Hopper | 9.0 | H100, H200 | 12.0 |
| Blackwell | 10.0, 12.0 | RTX 5090, B100/B200 | 12.8 |

### AMD (OpenCL)

Any GPU with OpenCL 1.2+ support. Tested on:
- RX 9070 XT (RDNA 4) via ROCm 7.2
- Should work on GCN, RDNA, RDNA 2/3/4 architectures

---

## Building from Source

### Quick Start (Experienced Users)

Already have build tools and dependencies? Here's the short version:

**Linux — OpenCL only (AMD GPUs):**
```bash
sudo apt-get install -y cmake g++ libmicrohttpd-dev libssl-dev libhwloc-dev \
    opencl-headers ocl-icd-opencl-dev
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
# Binary: ./bin/n0s-ryo-miner
```

**Linux — CUDA only (NVIDIA GPUs):**
```bash
sudo apt-get install -y cmake g++ libmicrohttpd-dev libssl-dev libhwloc-dev
# Also need: CUDA Toolkit 11.0+ from https://developer.nvidia.com/cuda-toolkit
mkdir build && cd build
cmake .. -DOpenCL_ENABLE=OFF -DCUDA_ENABLE=ON -DCUDA_ARCH="75;86" -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

**Linux — Container build (no local deps needed):**
```bash
./scripts/container-build.sh 12.6          # CUDA 12.6
./scripts/container-build-opencl.sh        # OpenCL
```

**Windows — MSVC + CUDA (PowerShell):**
```powershell
.\scripts\build-windows.ps1 -CudaEnable -CudaArch "75;86"
```

---

### Ubuntu/Linux — Detailed Build Guide

This section walks you through building on Ubuntu 22.04/24.04 from scratch. Other Debian-based distros (Mint, Pop!_OS, etc.) should work with the same commands.

#### Step 1: Install Build Tools

You need a C++ compiler and CMake (the build system):

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

**What these are:**
- `build-essential` — GCC/G++ compiler, make, and standard C/C++ libraries
- `cmake` — Build system generator (version 3.18+ required)
- `git` — To clone the repository

Verify CMake version (must be 3.18+):
```bash
cmake --version
```

> **Ubuntu 20.04 users:** The default CMake may be too old. Add the Kitware PPA:
> ```bash
> sudo apt-get install -y apt-transport-https ca-certificates gnupg
> curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg
> echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main" | sudo tee /etc/apt/sources.list.d/kitware.list
> sudo apt-get update && sudo apt-get install -y cmake
> ```

#### Step 2: Install Dependencies

```bash
sudo apt-get install -y \
    libmicrohttpd-dev \
    libssl-dev \
    libhwloc-dev
```

**What these are:**
- `libmicrohttpd-dev` — Embedded HTTP server for the monitoring web dashboard
- `libssl-dev` — OpenSSL for TLS-encrypted pool connections
- `libhwloc-dev` — Hardware locality library for NUMA-aware memory allocation

All three are optional but recommended. See [Build Options Reference](#build-options-reference) to disable any of them.

#### Step 3: Install GPU Backend Dependencies

Choose based on your GPU:

##### Option A: AMD GPU (OpenCL)

```bash
sudo apt-get install -y opencl-headers ocl-icd-opencl-dev
```

**What these are:**
- `opencl-headers` — OpenCL API header files (needed at compile time)
- `ocl-icd-opencl-dev` — OpenCL ICD (Installable Client Driver) loader development files

> **Note:** You also need a working OpenCL runtime (GPU driver) installed to *run* the miner. For AMD, this is typically [ROCm](https://rocm.docs.amd.com/) or the AMDGPU-PRO driver. The build only needs the headers and ICD loader.

If using ROCm and CMake can't find OpenCL, specify the paths explicitly:
```bash
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON \
    -DOpenCL_INCLUDE_DIR=/opt/rocm/include \
    -DOpenCL_LIBRARY=/usr/lib/x86_64-linux-gnu/libOpenCL.so
```

##### Option B: NVIDIA GPU (CUDA)

Install the CUDA Toolkit from NVIDIA. The version you need depends on your GPU:
- **GTX 10xx / RTX 20xx / RTX 30xx:** CUDA 11.8 is sufficient
- **RTX 40xx:** CUDA 11.8+
- **RTX 50xx:** CUDA 12.8+

Install from NVIDIA's repository (Ubuntu 22.04 example):
```bash
# Add NVIDIA's package repository
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update

# Install CUDA Toolkit (compiler + libraries)
sudo apt-get install -y cuda-toolkit-12-6
```

Or download from [developer.nvidia.com/cuda-toolkit](https://developer.nvidia.com/cuda-toolkit).

After installation, make sure CUDA is in your PATH:
```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH

# Verify
nvcc --version
```

> **Tip:** Add those `export` lines to your `~/.bashrc` to make them permanent.

##### Option C: Both AMD + NVIDIA

Install everything from both Option A and Option B above.

#### Step 4: Clone and Build

```bash
git clone https://github.com/n0sn0de/n0s-ryo-miner.git
cd n0s-ryo-miner
mkdir build && cd build
```

Now configure with CMake. Choose the command that matches your setup:

**AMD only (OpenCL):**
```bash
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON -DCMAKE_BUILD_TYPE=Release
```

**NVIDIA only (CUDA):**
```bash
cmake .. -DOpenCL_ENABLE=OFF -DCUDA_ENABLE=ON \
    -DCUDA_ARCH="75;86" \
    -DCMAKE_BUILD_TYPE=Release
```

**Both backends:**
```bash
cmake .. -DCUDA_ENABLE=ON -DOpenCL_ENABLE=ON \
    -DCUDA_ARCH="75;86" \
    -DCMAKE_BUILD_TYPE=Release
```

> **Set `CUDA_ARCH` to match your GPU!** Common values:
> | GPU | Architecture | CUDA_ARCH value |
> |---|---|---|
> | GTX 1060/1070/1080 | Pascal | `61` |
> | RTX 2060/2070/2080 | Turing | `75` |
> | RTX 3060/3070/3080/3090 | Ampere | `86` |
> | RTX 4060/4070/4080/4090 | Ada Lovelace | `89` |
> | H100/H200 | Hopper | `90` |
> | RTX 5090 / B200 | Blackwell | `100;120` |
>
> You can target multiple architectures: `-DCUDA_ARCH="75;86;89"` (builds take longer but binary works on all listed GPUs).

**Build:**
```bash
cmake --build . -j$(nproc)
```

This compiles using all CPU cores. It takes 1-5 minutes depending on how many CUDA architectures you're targeting.

**Verify it worked:**
```bash
./bin/n0s-ryo-miner --version
```

You should see output like:
```
n0s-ryo-miner v3.x.x (<commit>) [<backends>]
```

#### Step 5: Portable / Release Builds

If you want to build a binary that runs on any x86_64 Linux machine (not optimized for your specific CPU):

```bash
cmake .. -DN0S_COMPILE=generic -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

The `N0S_COMPILE=generic` flag disables `-march=native` so the binary isn't tied to your CPU's instruction set.

#### Troubleshooting (Linux)

| Problem | Solution |
|---|---|
| `CMake Error: Could not find CUDA` | Install CUDA Toolkit and ensure `nvcc` is in PATH |
| `CMake Error: OpenCL NOT found` | `sudo apt-get install opencl-headers ocl-icd-opencl-dev` |
| `CMake Error: microhttpd NOT found` | `sudo apt-get install libmicrohttpd-dev` or disable with `-DMICROHTTPD_ENABLE=OFF` |
| `CMake Error: OpenSSL NOT found` | `sudo apt-get install libssl-dev` or disable with `-DOpenSSL_ENABLE=OFF` |
| `CMake Error: hwloc NOT found` | `sudo apt-get install libhwloc-dev` or disable with `-DHWLOC_ENABLE=OFF` |
| CMake version too old | See Ubuntu 20.04 note above for Kitware PPA |
| `nvcc fatal: Unsupported gpu architecture 'compute_XX'` | Your CUDA Toolkit doesn't support that arch. Lower `CUDA_ARCH` or upgrade CUDA |
| Build succeeds but miner says "No OpenCL platforms" | You need a GPU runtime driver (ROCm, AMDGPU-PRO, NVIDIA driver) — the build only needs headers |
| `#pragma message: CL_TARGET_OPENCL_VERSION is not defined` | This is a harmless warning, not an error. Build is fine. |

---

### Container Builds (Docker/Podman)

Build without installing any dependencies on your host machine. Just need Docker or Podman.

This is the **recommended approach** for producing release binaries — reproducible, clean, no system conflicts.

#### Prerequisites

Install Podman (recommended) or Docker:

```bash
# Podman (Ubuntu 22.04+)
sudo apt-get install -y podman

# Or Docker
# See https://docs.docker.com/engine/install/ubuntu/
```

#### Build Commands

```bash
# OpenCL (AMD GPUs)
./scripts/container-build-opencl.sh

# CUDA 11.8 (Pascal→Ada)
./scripts/container-build.sh 11.8

# CUDA 12.6 (Pascal→Hopper)
./scripts/container-build.sh 12.6

# CUDA 12.8 (Pascal→Blackwell)
./scripts/container-build.sh 12.8

# Full build matrix (all 4 backends)
./scripts/build-matrix.sh
```

Artifacts appear in `dist/opencl-ubuntu22.04/` or `dist/cuda-{version}/`.

#### Advanced Container Options

```bash
# Custom architectures
./scripts/container-build.sh 11.8 "61;75;86"

# Different Ubuntu base
./scripts/container-build.sh 12.6 "" 24.04

# Full validation suite (10 platform combos, compile-only)
./scripts/matrix-test.sh

# Quick smoke test (1 CUDA + 1 OpenCL build)
./scripts/matrix-test.sh --quick
```

#### Why Container Builds?

- ✅ **No local CUDA toolkit required** — just Podman/Docker
- ✅ **Reproducible** — same source → same binaries every time
- ✅ **Multiple CUDA versions** — build for 11.8, 12.6, and 12.8 on the same machine
- ✅ **Clean environment** — no conflicts with system libraries
- ✅ **CI/CD ready** — same scripts work in GitHub Actions / GitLab CI

---

### Windows — Native MSVC Build

Build natively on Windows using Visual Studio and the CUDA Toolkit. This produces the fastest Windows binary.

#### Prerequisites

Install the following (in order):

##### 1. Visual Studio 2022

Download [Visual Studio 2022 Community](https://visualstudio.microsoft.com/downloads/) (free).

During installation, select the **"Desktop development with C++"** workload. This includes:
- MSVC compiler
- CMake (ships with VS)
- Ninja build system
- Windows SDK

##### 2. CUDA Toolkit (for NVIDIA builds)

Download from [developer.nvidia.com/cuda-toolkit](https://developer.nvidia.com/cuda-toolkit).

- **RTX 20xx/30xx:** CUDA 11.8+ works
- **RTX 40xx:** CUDA 11.8+ works
- **RTX 50xx:** CUDA 12.8+ required

During installation, select at minimum:
- CUDA Development → Compiler, Libraries, Headers
- CUDA Development → NSight (optional)

After installation, verify in PowerShell:
```powershell
nvcc --version
```

> **If `nvcc` is not found:** The installer usually adds it to PATH automatically. If not, add `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin` (adjust version) to your system PATH.

##### 3. vcpkg (Dependency Manager)

vcpkg fetches and builds C++ libraries (OpenSSL, libmicrohttpd) automatically.

```powershell
cd $env:USERPROFILE
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

Set the environment variable so the build script finds it:
```powershell
# For current session:
$env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"

# To make it permanent (run once):
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "$env:USERPROFILE\vcpkg", "User")
```

Install the required libraries:
```powershell
.\vcpkg.exe install libmicrohttpd:x64-windows-static openssl:x64-windows-static
```

This takes a few minutes the first time. vcpkg compiles them from source.

##### 4. Ninja (Build Tool)

If you installed the full C++ workload in Visual Studio, Ninja is already available inside the "Developer PowerShell for VS 2022". Otherwise:
```powershell
# Via winget
winget install Ninja-build.Ninja

# Or via vcpkg's bundled version (already available if vcpkg is set up)
```

#### Build Steps

**Option A: Use the build script (recommended)**

Open **"Developer PowerShell for VS 2022"** (search in Start menu) and run:

```powershell
cd path\to\n0s-ryo-miner

# CUDA build
.\scripts\build-windows.ps1 -CudaEnable -CudaArch "75;86;89"

# OpenCL build
.\scripts\build-windows.ps1 -OpenclEnable

# Both
.\scripts\build-windows.ps1 -CudaEnable -OpenclEnable -CudaArch "75;86;89"

# Clean rebuild
.\scripts\build-windows.ps1 -CudaEnable -Clean
```

The script handles vcpkg integration, CMake configuration, and building automatically.

**Option B: Manual CMake commands**

Open **"Developer PowerShell for VS 2022"** and run:

```powershell
cd path\to\n0s-ryo-miner
mkdir build
cd build

# Configure (CUDA + static linking via vcpkg)
cmake -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DCMAKE_LINK_STATIC=ON `
    -DN0S_COMPILE=generic `
    -DCUDA_ENABLE=ON `
    -DCUDA_ARCH="75;86;89" `
    -DOpenCL_ENABLE=OFF `
    -DHWLOC_ENABLE=OFF `
    ..

# Build
cmake --build . -j $env:NUMBER_OF_PROCESSORS
```

> **Important:** Use the **Developer PowerShell** (not regular PowerShell). It sets up the MSVC compiler environment. Without it, CMake won't find `cl.exe`.

**Verify the build:**
```powershell
.\bin\n0s-ryo-miner.exe --version
```

Expected output:
```
n0s-ryo-miner v3.x.x (<commit>) [nvidia] win/generic
```

#### Troubleshooting (Windows)

| Problem | Solution |
|---|---|
| `cmake` not found | Use "Developer PowerShell for VS 2022" — it adds CMake to PATH |
| `nvcc` not found | Add CUDA Toolkit `bin` directory to PATH |
| `Ninja` not found | Install via `winget install Ninja-build.Ninja` or use VS Developer PowerShell |
| vcpkg libraries not found | Ensure `$env:VCPKG_ROOT` is set and you ran `vcpkg install` |
| `LINK : fatal error LNK1104: cannot open file 'libmicrohttpd.lib'` | Run `vcpkg install libmicrohttpd:x64-windows-static` |
| OpenSSL link errors | Run `vcpkg install openssl:x64-windows-static` |
| `error MSB8020: The build tools for v143 cannot be found` | Install "Desktop development with C++" workload in VS Installer |
| Build works but miner crashes at startup | Ensure GPU drivers are up to date. CUDA requires matching driver version. |
| `CUDA_ERROR_NO_DEVICE` | No NVIDIA GPU detected. Check driver installation with `nvidia-smi` |

---

### Windows — MinGW Cross-Compile (from Linux)

Build a Windows `.exe` from a Linux machine using the MinGW-w64 cross-compiler. Useful for CI/CD or if you develop on Linux but need Windows binaries.

> **Note:** This method builds an **OpenCL-only** Windows binary (no CUDA). For CUDA on Windows, use the native MSVC build above.

```bash
# Install cross-compiler
sudo apt-get install -y g++-mingw-w64-x86-64-posix

# Build (downloads and cross-compiles OpenSSL + libmicrohttpd automatically)
./scripts/cross-build-windows.sh

# Build + test with Wine
./scripts/cross-build-windows.sh --test

# Clean build
./scripts/cross-build-windows.sh --clean
```

Output: `dist/windows-opencl/n0s-ryo-miner.exe`

The script produces a fully static Windows PE64 binary with TLS and HTTP dashboard support. At runtime on Windows, the GPU vendor's `OpenCL.dll` (AMD or NVIDIA) provides the actual OpenCL implementation.

---

### Build Options Reference

All CMake options with their defaults:

| Option | Default | Description |
|---|---|---|
| `CUDA_ENABLE` | `ON` | Build NVIDIA CUDA backend |
| `OpenCL_ENABLE` | `ON` | Build AMD OpenCL backend |
| `CUDA_ARCH` | auto | Target GPU architectures (e.g. `"75;86;89"`) |
| `MICROHTTPD_ENABLE` | `ON` | Build HTTP monitoring dashboard |
| `OpenSSL_ENABLE` | `ON` | Enable TLS for encrypted pool connections |
| `HWLOC_ENABLE` | `ON` | Enable NUMA-aware memory allocation |
| `CMAKE_BUILD_TYPE` | `Release` | `Release` (optimized) or `Debug` |
| `N0S_COMPILE` | `native` | `native` (tuned for your CPU) or `generic` (portable) |
| `CMAKE_LINK_STATIC` | `OFF` | Link libraries statically (for self-contained binaries) |

**Disable an optional dependency:**
```bash
cmake .. -DMICROHTTPD_ENABLE=OFF -DOpenSSL_ENABLE=OFF -DHWLOC_ENABLE=OFF
```

This builds a minimal binary with no HTTP dashboard, no TLS, and no NUMA support — useful for embedded or minimal environments.

---

## Usage

```bash
./build/bin/n0s-ryo-miner -o pool:port -u wallet_address -p x
```

### Autotune (Recommended for First Run)

Find optimal GPU settings automatically before mining:

```bash
# Quick autotune — tests ~3-9 candidates, takes 2-6 minutes
./build/bin/n0s-ryo-miner --autotune

# Balanced — more candidates, better coverage (~5-10 minutes)
./build/bin/n0s-ryo-miner --autotune --autotune-mode balanced

# AMD only or NVIDIA only
./build/bin/n0s-ryo-miner --autotune --autotune-backend amd
./build/bin/n0s-ryo-miner --autotune --autotune-backend nvidia

# Re-tune (ignore cached results)
./build/bin/n0s-ryo-miner --autotune --autotune-reset
```

Autotune writes optimized settings to `amd.txt` / `nvidia.txt` and caches results in `autotune.json`. Subsequent runs reuse cached settings automatically unless hardware or drivers change.

**How it works:**
1. Discovers GPUs and collects device fingerprints
2. Generates candidate parameter sets based on GPU architecture
3. Benchmarks each candidate in an isolated subprocess (crash-safe)
4. Scores candidates by hashrate stability (penalizes variance and errors)
5. Runs extended stability validation on the winner
6. Writes optimal config files ready for mining

| Autotune Flag | Description |
|---|---|
| `--autotune` | Enable autotune mode |
| `--autotune-mode quick\|balanced\|exhaustive` | Search depth (default: `balanced`) |
| `--autotune-backend amd\|nvidia\|all` | Which GPUs to tune (default: `all`) |
| `--autotune-gpu 0,1` | Specific GPU indices to tune |
| `--autotune-reset` | Ignore cached results, re-tune from scratch |
| `--autotune-resume` | Resume an interrupted autotune run |
| `--autotune-benchmark-seconds N` | Per-candidate benchmark time (default: 30) |
| `--autotune-stability-seconds N` | Final stability validation time (default: 60) |
| `--autotune-target hashrate\|efficiency\|balanced` | Optimization target (default: `hashrate`) |
| `--autotune-export PATH` | Export results to a specific file |

### Command-Line Options

| Flag | Description |
|---|---|
| `-o pool:port` | Pool address |
| `-u wallet` | Wallet address |
| `-p password` | Pool password (usually `x`) |
| `--noAMD` | Disable AMD GPU backend |
| `--noNVIDIA` | Disable NVIDIA GPU backend |
| `--noAMDCache` | Don't cache compiled OpenCL kernels |
| `--benchmark N` | Run benchmark for block version N and exit |
| `--benchmark-json FILE` | Write benchmark results as JSON |
| `--profile` | Enable per-kernel phase timing (use with `--benchmark`) |

### Configuration Files

Generated on first run (or by `--autotune`) in the working directory:
- **`pools.txt`** — Pool connection settings
- **`amd.txt`** — AMD GPU settings (intensity, worksize per GPU)
- **`nvidia.txt`** — NVIDIA GPU settings (threads, blocks, bfactor per GPU)
- **`autotune.json`** — Cached autotune results with device fingerprints

### HTTP Monitoring API

When built with libmicrohttpd (`-DMICROHTTPD_ENABLE=ON`, the default):
```
http://localhost:<port>/api.json
```

Returns JSON with hashrate, pool stats, and GPU status. Port is configured in the miner's config.

---

## Benchmarks

Tested on 3 GPU architectures with autotuned settings:

| GPU | Architecture | VRAM | Optimal Settings | Hashrate |
|---|---|---|---|---|
| AMD RX 9070 XT | RDNA 4 (gfx1201) | 16 GB | intensity=1536, worksize=8 | **4,531 H/s** |
| NVIDIA RTX 2070 | Turing (sm_75) | 8 GB | threads=8, blocks=216 | **2,160 H/s** |
| NVIDIA GTX 1070 Ti | Pascal (sm_61) | 8 GB | threads=8, blocks=114 | **1,687 H/s** |

*Settings discovered via `--autotune --autotune-mode quick`. Your results may vary by ±5% due to thermal conditions, driver version, and system load.*

### Per-Phase Kernel Profiling

Use `--benchmark 10 --profile` to see where time is spent:

| Phase | Description | RX 9070 XT | GTX 1070 Ti | RTX 2070 |
|---|---|---|---|---|
| Phase 1 | Keccak init | <0.1% | <0.1% | <0.1% |
| Phase 2 | Scratchpad fill | 12.0% | 2.5% | 3.1% |
| **Phase 3** | **GPU compute (FP math)** | **69.5%** | **82.4%** | **85.3%** |
| Phase 4+5 | AES implode + finalize | 18.5% | 15.1% | 11.7% |

Phase 3 (cooperative floating-point computation with data-dependent memory access) dominates on all architectures. The algorithm is designed to be GPU-friendly but resistant to algorithmic shortcuts.

---

## Testing

### Unit Tests
```bash
bash tests/build_harness.sh && ./tests/cn_gpu_harness   # Golden hash verification
bash tests/build_autotune_test.sh                       # Autotune framework (21 tests)
```

### Local AMD Test
```bash
./test-mine.sh                         # Build + mine for 30s on local AMD GPU
```

### Remote NVIDIA Test
```bash
REMOTE=nos2 ./test-mine-remote.sh      # Build on remote, mine for 40s
```

### Triple-GPU Integration Test
```bash
./test-both.sh                         # AMD + NVIDIA Pascal + NVIDIA Turing
```

### Containerized Compile Matrix
```bash
./scripts/matrix-test.sh               # 10 platform combos, compile-only
./scripts/matrix-test.sh --test        # Compile + hardware mine tests
```

---

## Project Structure

```
├── CMakeLists.txt              # Build system (C++17, CUDA + OpenCL)
├── scripts/
│   ├── build-windows.ps1       # Windows MSVC build helper
│   ├── cross-build-windows.sh  # MinGW cross-compile (Linux → Windows)
│   ├── container-build.sh      # Containerized CUDA build (podman)
│   ├── container-build-opencl.sh # Containerized OpenCL build
│   ├── build-matrix.sh         # Build all CUDA versions + optional test
│   ├── matrix-test.sh          # Full 10-platform compile validation
│   ├── test-remote-binary.sh   # Deploy + mine-test on remote GPU host
│   └── test-nosnode.sh         # nosnode-specific test script
├── cmake/
│   └── mingw-w64-x86_64.cmake # MinGW cross-compile toolchain file
├── n0s/
│   ├── cli/                    # CLI entry point
│   ├── autotune/               # GPU autotuning framework
│   ├── backend/
│   │   ├── amd/                # AMD OpenCL backend
│   │   │   └── amd_gpu/opencl/ # OpenCL kernels (.cl)
│   │   ├── nvidia/             # NVIDIA CUDA backend
│   │   │   └── nvcc_code/      # CUDA kernels (.cu, .hpp)
│   │   └── cpu/                # Shared crypto library (hash verification)
│   ├── net/                    # Pool connection (Stratum)
│   ├── http/                   # HTTP monitoring API
│   └── misc/                   # Utilities, logging, config
├── docs/
│   └── CN-GPU-WHITEPAPER.md    # CryptoNight-GPU algorithm deep dive
├── test-mine.sh                # Local AMD mining test
├── test-mine-remote.sh         # Remote NVIDIA mining test
└── test-both.sh                # Triple-GPU integration test
```

---

## Algorithm

This miner implements **CryptoNight-GPU** (`cn/gpu`), a proof-of-work algorithm designed specifically for GPU mining. It uses:
- Keccak-1600 for state initialization
- A 2 MiB scratchpad filled via Keccak permutations
- A GPU-optimized main loop with 16-thread cooperative groups performing floating-point math, creating a workload that favors massively parallel GPU architectures
- AES + mix-and-propagate finalization
- Final Keccak hash for proof verification

See [`docs/CN-GPU-WHITEPAPER.md`](docs/CN-GPU-WHITEPAPER.md) for the complete algorithm specification.

---

## License

GPLv3 — see [LICENSE](LICENSE).

## Credits

- **fireice-uk** and **psychocrypt** — original xmr-stak
- **RYO Currency team** — CryptoNight-GPU algorithm design
- **n0sn0de** — modernization, cleanup, and ongoing development
