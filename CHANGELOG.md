# Changelog

## v3.2.0 — Single Binary + GUI Dashboard (2026-04-02)

### Single Executable (Pillar 1)

- **One binary, zero companion files.** Eliminated the entire `dlopen`/`dlsym` plugin system.
- Both CUDA and OpenCL backends compile as static libraries and link directly into the executable.
- Removed `plugin.hpp` (81 lines of dlopen/dlsym/dlclose abstraction).
- Removed `-ldl` dependency.
- Simplified CMake install rules to single binary.

### GUI Dashboard (Pillar 2)

- **Embedded web dashboard** — modern dark-themed SPA served from the miner binary.
- **9 REST API v1 endpoints** with clean JSON responses:
  - `/api/v1/status` — Mining state, uptime, pool connection
  - `/api/v1/hashrate` — Per-GPU + total hashrate (10s/60s/15m windows)
  - `/api/v1/hashrate/history` — Time-series ring buffer (3600 samples, 1s resolution)
  - `/api/v1/gpus` — GPU telemetry with device names, temp/power/fan/clocks, H/W efficiency
  - `/api/v1/pool` — Shares, difficulty, ping, top difficulties
  - `/api/v1/config` — Pool configuration (wallet masked for security)
  - `/api/v1/autotune` — Cached autotune results per GPU
  - `/api/v1/version` — Version, build info, enabled backends
- **Hashrate history ring buffer** — 3600 samples, 1-second resolution, ~115 KB memory
- **Real-time hashrate chart** — Pure `<canvas>` drawing, per-GPU lines + total with gradient fill
- **GPU telemetry table** — Device names (nvidia-smi / amd-smi), temp coloring, H/W efficiency
- **Tab navigation** — Monitor (live data) + Configuration pages
- **Responsive design** — Mobile-friendly with horizontal scroll for GPU table
- **Pre-gzipped embedded assets** — `Content-Encoding: gzip`, zero runtime compression
- **Total frontend size: 6.1 KB gzipped** (12% of 50 KB budget)
- **`--gui` flag** — Opens browser to dashboard; mining continues in CLI
- **`--gui-dev DIR`** — Hot-reload development mode (serve from filesystem)
- **Legacy endpoints preserved** — `/h`, `/c`, `/r`, `/api.json`, `/style.css` all still work

### Performance (carried from v3.1.x optimization sessions)

| GPU | Hashrate | vs v3.1.0 |
|---|---|---|
| AMD RX 9070 XT (OpenCL) | 5,069 H/s | +12% |
| NVIDIA GTX 1070 Ti (CUDA 11.8) | 1,631 H/s | −3% (intensity rebalance) |
| NVIDIA RTX 2070 (CUDA 12.6) | 2,236 H/s | +4% |

### Build Sizes

| Variant | Size |
|---|---|
| OpenCL-only | 1.0 MB |
| CUDA 11.8 | 3.0 MB |
| CUDA 12.6 | 3.5 MB |
| Container (CUDA 11.8) | 2.3 MB |

---

## v3.1.0 — GPU Autotune (2026-03-31)

### New Features

- **`--autotune` — Automatic GPU parameter discovery**
  - One-command optimal settings for any AMD/OpenCL or NVIDIA/CUDA GPU
  - Quick (2-6 min), Balanced (5-10 min), and Exhaustive modes
  - Subprocess-based evaluation with crash isolation (OOM-safe)
  - Device fingerprinting with cached results in `autotune.json`
  - Stability validation: penalizes high variance and error rates
  - Writes optimized `amd.txt` / `nvidia.txt` configs automatically
  - 10 CLI flags for full control over the tuning process

- **CryptoNight-GPU kernel-aware candidate generation**
  - NVIDIA: threads=8 fixed (128 threads/block matching `__launch_bounds__`)
  - NVIDIA: blocks sweep around architecture-optimal (Pascal=7×SM, Turing+=6×SM)
  - AMD: intensity/worksize sweep with CU-aligned candidates
  - Accurate per-hash memory calculations from actual kernel requirements

- **NVIDIA SM count estimation fallback**
  - Lookup table for 20+ GPU models (Pascal through Ada Lovelace)
  - Architecture-based fallback for unknown models
  - Robust nvidia-smi parsing with digit-prefix validation

### Documentation

- Comprehensive autotune section in README with CLI reference
- New `docs/AUTOTUNE.md` architecture guide
- Benchmark results table with 3-GPU validation data
- Per-phase kernel profiling table

### Testing

- 21 unit tests for autotune framework (scoring, candidates, persistence)
- Golden hash verification on all changes
- 3-GPU validation matrix (AMD RDNA4 + NVIDIA Pascal + NVIDIA Turing)

### Validated Performance (Autotuned)

| GPU | Settings | Hashrate |
|---|---|---|
| AMD RX 9070 XT | intensity=1536, worksize=8 | 4,531 H/s |
| NVIDIA RTX 2070 | threads=8, blocks=216 | 2,160 H/s |
| NVIDIA GTX 1070 Ti | threads=8, blocks=114 | 1,687 H/s |

---

## v3.0.0 — Modern C++ Rewrite (2026-03-29)

Complete modernization of the xmr-stak codebase:
- Pure C++17, Linux-only, zero C files
- `n0s::` namespace, `n0s/` directory structure
- Zero-warning build (`-Wall -Wextra`)
- Single-algorithm focus (CryptoNight-GPU only)
- Container build system (Podman/Docker)
- `--benchmark` with JSON output and stability CV%
- `--profile` for per-kernel phase timing
- Dead code removal, smart pointers, modern headers
