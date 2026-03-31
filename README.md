# n0s-ryo-miner ⚡

GPU miner for [RYO Currency](https://ryo-currency.com) using the **CryptoNight-GPU** algorithm.

Supports **AMD** (OpenCL) and **NVIDIA** (CUDA) GPUs. No CPU mining — this is a GPU-only miner.

Fork of [xmr-stak](https://github.com/fireice-uk/xmr-stak) by fireice-uk and psychocrypt, stripped down and modernized for CryptoNight-GPU exclusively.

**Latest Release:** [v3.1.0 - GPU Autotune](https://github.com/n0sn0de/n0s-ryo-miner/releases/tag/v3.1.0) 🚀

## Download Pre-Built Binaries

Grab the latest release from [GitHub Releases](https://github.com/n0sn0de/n0s-ryo-miner/releases):

| Platform | Binary | Backend Library | Architectures |
|---|---|---|---|
| **OpenCL (AMD)** | `n0s-ryo-miner-v3.1.0-opencl-ubuntu22.04` | `libn0s_opencl_backend-v3.1.0-ubuntu22.04.so` | GCN/RDNA/CDNA |
| **CUDA 11.8** | `n0s-ryo-miner-v3.1.0-cuda11.8` | `libn0s_cuda_backend-v3.1.0-cuda11.8.so` | Pascal→Ada (sm_61-89) |
| **CUDA 12.6** | `n0s-ryo-miner-v3.1.0-cuda12.6` | `libn0s_cuda_backend-v3.1.0-cuda12.6.so` | Pascal→Hopper (sm_61-90) |
| **CUDA 12.8** | `n0s-ryo-miner-v3.1.0-cuda12.8` | `libn0s_cuda_backend-v3.1.0-cuda12.8.so` | Pascal→Blackwell (sm_61-120) |

**Note:** Download BOTH the binary and backend library, place them in the same directory.

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

Pre-built binaries are produced for CUDA 11.8 (Pascal→Ada), CUDA 12.6 (Pascal→Hopper), and CUDA 12.8 (Pascal→Blackwell).

### AMD (OpenCL)

Any GPU with OpenCL 1.2+ support. Tested on:
- RX 9070 XT (RDNA 4) via ROCm 7.2
- Should work on GCN, RDNA, RDNA 2/3/4 architectures

## Build Requirements

| Dependency | Required | Notes |
|---|---|---|
| CMake | >= 3.18 | Kitware PPA for Ubuntu 18.04/20.04 |
| C++ compiler | GCC >= 7 or Clang >= 5 | C++17 required |
| CUDA Toolkit | >= 11.0 | For NVIDIA builds |
| OpenCL | any | For AMD builds (via ROCm, AMD APP SDK, or `ocl-icd`) |
| libmicrohttpd | optional | HTTP monitoring API |
| OpenSSL | optional | TLS pool connections |
| hwloc | optional | NUMA-aware memory allocation |

## Quick Build

### AMD Only (OpenCL)
```bash
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON
cmake --build . -j$(nproc)
```

For ROCm, you may need to specify paths:
```bash
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON \
  -DOpenCL_INCLUDE_DIR=/opt/rocm/include \
  -DOpenCL_LIBRARY=/usr/lib/x86_64-linux-gnu/libOpenCL.so
```

### NVIDIA Only (CUDA)
```bash
mkdir build && cd build
cmake .. -DOpenCL_ENABLE=OFF -DCUDA_ENABLE=ON -DCUDA_ARCH="61;75;86"
cmake --build . -j$(nproc)
```

Set `CUDA_ARCH` to match your GPU's compute capability. Common values:
- `61` — GTX 1060/1070/1080
- `75` — RTX 2060/2070/2080
- `86` — RTX 3060/3070/3080/3090
- `89` — RTX 4060/4070/4080/4090
- `90` — H100/H200
- `100;120` — RTX 5090/B200 (requires CUDA 12.8+)

### Both Backends
```bash
mkdir build && cd build
cmake .. -DCUDA_ARCH="61;75;86"
cmake --build . -j$(nproc)
```

### Portable / Release Builds
```bash
cmake .. -DN0S_COMPILE=generic -DCMAKE_BUILD_TYPE=Release
```

The `N0S_COMPILE=generic` flag avoids `-march=native` so binaries run on any x86_64 CPU.

## Container Builds (Recommended)

Build for **any CUDA version** without installing the toolkit locally. Uses Podman or Docker with official NVIDIA CUDA images.

### Quick Start

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

Artifacts land in `dist/opencl-ubuntu22.04/` or `dist/cuda-{version}/`.

### Advanced Options

```bash
# Custom architectures
./scripts/container-build.sh 11.8 "61,75,86"       # Specific compute capabilities
./scripts/container-build.sh 12.8 "auto"           # All supported archs for CUDA version

# Different Ubuntu base
./scripts/container-build.sh 12.6 "" 24.04         # Ubuntu 24.04 instead of 22.04

# Full validation suite (10 platform combos, compile-only)
./scripts/matrix-test.sh

# Quick smoke test (1 CUDA + 1 OpenCL build)
./scripts/matrix-test.sh --quick

# Filter specific platforms
./scripts/matrix-test.sh --filter "12.8"
./scripts/matrix-test.sh --filter "opencl"
```

### Why Container Builds?

- ✅ **No local CUDA toolkit required** — Just Podman/Docker
- ✅ **Reproducible** — Same source → same binaries every time
- ✅ **Multiple CUDA versions** — Build for 11.8, 12.6, and 12.8 on same machine
- ✅ **Clean environment** — No conflicts with system libraries
- ✅ **CI/CD ready** — Same scripts work in GitHub Actions / GitLab CI

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

## Project Structure

```
├── CMakeLists.txt              # Build system (C++17, CUDA + OpenCL)
├── scripts/
│   ├── container-build.sh      # Containerized CUDA build (podman)
│   ├── build-matrix.sh         # Build all CUDA versions + optional test
│   ├── matrix-test.sh          # Full 10-platform compile validation
│   ├── test-remote-binary.sh   # Deploy + mine-test on remote GPU host
│   └── test-nosnode.sh         # nosnode-specific test script
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

## Algorithm

This miner implements **CryptoNight-GPU** (`cn/gpu`), a proof-of-work algorithm designed specifically for GPU mining. It uses:
- Keccak-1600 for state initialization
- A 2 MiB scratchpad filled via Keccak permutations
- A GPU-optimized main loop with 16-thread cooperative groups performing floating-point math, creating a workload that favors massively parallel GPU architectures
- AES + mix-and-propagate finalization
- Final Keccak hash for proof verification

See [`docs/CN-GPU-WHITEPAPER.md`](docs/CN-GPU-WHITEPAPER.md) for the complete algorithm specification.

## License

GPLv3 — see [LICENSE](LICENSE).

## Credits

- **fireice-uk** and **psychocrypt** — original xmr-stak
- **RYO Currency team** — CryptoNight-GPU algorithm design
- **n0sn0de** — modernization, cleanup, and ongoing development
