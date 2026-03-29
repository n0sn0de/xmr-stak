# n0s-ryo-miner ⚡

GPU miner for [RYO Currency](https://ryo-currency.com) using the CryptoNight-GPU algorithm.

Supports **AMD** (OpenCL) and **NVIDIA** (CUDA) GPUs.

Based on [xmr-stak](https://github.com/fireice-uk/xmr-stak) by fireice-uk and psychocrypt.

## Supported Hardware

### AMD (OpenCL)
Any GPU with OpenCL support. Tested on:
- RX 9070 XT (RDNA 4) via ROCm 7.2

### NVIDIA (CUDA)
Minimum: Pascal (GTX 10xx). Supported architectures:

| Architecture | Compute | Example GPUs | Min CUDA |
|---|---|---|---|
| Pascal | 6.1 | GTX 1060/1070/1080 | 11.0 |
| Turing | 7.5 | RTX 2060/2070/2080 | 11.0 |
| Ampere | 8.0, 8.6 | RTX 3060/3070/3080/3090 | 11.0 |
| Ada Lovelace | 8.9 | RTX 4060/4070/4080/4090 | 11.8 |
| Hopper | 9.0 | H100 | 12.0 |
| Blackwell | 10.0, 12.0 | RTX 5090, B100/B200 | 12.8 |

## Build Requirements

- CMake >= 3.18
- C++17 compiler (GCC >= 7, Clang >= 5)
- CUDA >= 11.0 (for NVIDIA)
- OpenCL (for AMD, via ROCm or AMD APP SDK)
- libmicrohttpd (optional, for HTTP monitoring)
- OpenSSL (optional, for TLS pool connections)
- hwloc (optional, for NUMA-aware memory)

## Quick Build

### AMD Only
```bash
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF
cmake --build . -j$(nproc)
```

### NVIDIA Only
```bash
mkdir build && cd build
cmake .. -DOpenCL_ENABLE=OFF -DCUDA_ARCH="61;75;86"
cmake --build . -j$(nproc)
```

### Both
```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

## Usage

```bash
./build/bin/n0s-ryo-miner -o pool:port -u wallet_address -p x
```

### Options
- `--noAMD` — Disable AMD GPU backend
- `--noNVIDIA` — Disable NVIDIA GPU backend
- `--noAMDCache` — Don't cache compiled OpenCL kernels
- `-o pool:port` — Pool address
- `-u wallet` — Wallet address
- `-p password` — Pool password (usually `x`)

## Configuration Files

Generated on first run:
- `pools.txt` — Pool configuration
- `amd.txt` — AMD GPU settings (intensity, worksize)
- `nvidia.txt` — NVIDIA GPU settings

## HTTP API

When built with microhttpd, an HTTP API is available for monitoring:
- `http://localhost:port/api.json` — JSON stats

## License

GPLv3 — see [LICENSE](LICENSE).
