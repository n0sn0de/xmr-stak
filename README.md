# n0s-cngpu

**CryptoNight-GPU miner for [RYO Currency](https://ryo-currency.com)**

A dedicated, single-algorithm miner built for the CryptoNight-GPU proof-of-work. Supports AMD (OpenCL) and NVIDIA (CUDA) GPUs, plus CPU mining.

> Fork of [xmr-stak](https://github.com/fireice-uk/xmr-stak) by fireice-uk & psychocrypt — stripped down to one algorithm, zero dev fees, clean builds.

---

## Features

- **CryptoNight-GPU only** — no bloat, no algorithm selection, just cn-gpu
- **Zero developer fees** — 100% of your hashrate goes to your pool
- **AMD + NVIDIA + CPU** — OpenCL and CUDA backends, plus CPU fallback
- **Multi-distro builds** — tested on Ubuntu 18.04, 20.04, 22.04, and 24.04
- **CI/CD** — automated builds and releases via GitHub Actions
- **GPLv3** — free and open source

## Quick Start

### Install dependencies (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake libmicrohttpd-dev libssl-dev libhwloc-dev
```

### Build (CPU-only)

```bash
git clone https://github.com/n0sn0de/xmr-stak.git n0s-cngpu
cd n0s-cngpu
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=OFF
make -j$(nproc)
```

### Build with AMD/OpenCL

```bash
sudo apt install ocl-icd-opencl-dev opencl-headers
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON
make -j$(nproc)
```

### Build with NVIDIA/CUDA

```bash
# Install CUDA toolkit first: https://developer.nvidia.com/cuda-downloads
mkdir build && cd build
cmake .. -DCUDA_ENABLE=ON -DOpenCL_ENABLE=OFF
make -j$(nproc)
```

### Run

```bash
./bin/n0s-cngpu -o pool.ryo-currency.com:3333 -u YOUR_WALLET_ADDRESS -p x
```

On first run, the miner creates `config.txt`, `pools.txt`, and backend config files (`cpu.txt`, `amd.txt`, `nvidia.txt`) in the current directory. Edit `pools.txt` to configure your pool and wallet.

## Supported Platforms

| Ubuntu Version | Codename | GCC | CMake | Status |
|---|---|---|---|---|
| 18.04 LTS | Bionic | 7 | 3.10 | ✅ |
| 20.04 LTS | Focal | 9 | 3.16 | ✅ |
| 22.04 LTS | Jammy | 11 | 3.22 | ✅ |
| 24.04 LTS | Noble | 13 | 3.28 | ✅ |

Other Linux distributions should work with equivalent compiler/library versions.

## Configuration

### Pool Configuration (`pools.txt`)

```json
"pool_list" :
[
    {
        "pool_address" : "pool.ryo-currency.com:3333",
        "wallet_address" : "YOUR_WALLET_ADDRESS",
        "rig_id" : "",
        "pool_password" : "x",
        "use_nicehash" : false,
        "use_tls" : false,
        "tls_fingerprint" : "",
        "pool_weight" : 1
    }
],
```

### Command Line Options

```
  -o, --url         Pool URL (host:port)
  -u, --user        Wallet address
  -p, --pass        Pool password (default: x)
  -r, --rigid       Rig identifier
  --currency        Ignored (always cryptonight_gpu)
  --benchmark       Run 60s benchmark (no pool connection)
  --noUAC           Skip UAC elevation prompt
  --help            Show all options
```

### HTTP API

The miner includes a built-in HTTP server for monitoring. Configure in `config.txt`:

```json
"httpd_port" : 8080,
```

Then visit `http://localhost:8080/api.json` for hashrate and connection stats.

## Build Options (CMake)

| Option | Default | Description |
|---|---|---|
| `CUDA_ENABLE` | ON | Build NVIDIA CUDA backend |
| `OpenCL_ENABLE` | ON | Build AMD OpenCL backend |
| `MICROHTTPD_ENABLE` | ON | Build HTTP API server |
| `OpenSSL_ENABLE` | ON | Enable TLS pool connections |
| `CMAKE_BUILD_TYPE` | Release | Build type (Release/Debug) |
| `XMR-STAK_COMPILE` | native | CPU optimization (native/generic) |

For portable binaries, use `-DXMR-STAK_COMPILE=generic`.

## Documentation

- [Compilation Guide](doc/compile/compile_Linux.md) — detailed build instructions
- [Usage Guide](doc/usage.md) — configuration and operation
- [Tuning Guide](doc/tuning.md) — GPU and CPU performance tuning
- [Troubleshooting](doc/troubleshooting.md) — common issues and fixes
- [FAQ](doc/FAQ.md) — frequently asked questions
- [Contributing](CONTRIBUTING.md) — how to contribute

## Release Builds

Pre-built binaries are available on the [Releases](https://github.com/n0sn0de/xmr-stak/releases) page for all supported Ubuntu versions. Each release includes:

- Statically optimized binary
- SHA256 checksum
- LICENSE and README

## License

**GPLv3** — see [LICENSE](LICENSE) for full text.

Fork of [xmr-stak](https://github.com/fireice-uk/xmr-stak) — original copyright © 2017–2019 fireice-uk, psychocrypt.
See [NOTICE](NOTICE) for fork lineage and [THIRD-PARTY-LICENSES](THIRD-PARTY-LICENSES) for upstream attributions.

## Links

- [RYO Currency](https://ryo-currency.com) — the coin we mine
- [n0sn0de](https://github.com/n0sn0de) — project maintainer
