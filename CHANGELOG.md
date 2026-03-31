# Changelog

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
