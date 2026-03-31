# Autotune Architecture

**`n0s-ryo-miner --autotune`** — automatic GPU parameter discovery for CryptoNight-GPU.

## Overview

The autotune system discovers optimal launch parameters for each GPU by running controlled benchmark experiments. It's designed for:

- **First-run setup** — No manual config editing required
- **Hardware changes** — Re-tunes when GPU/driver changes are detected
- **Reproducibility** — Results cached per device fingerprint

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│ autotune_entry.cpp — do_autotune()                      │
│   Parses CLI flags → discovers GPUs → orchestrates      │
│   Per-GPU: fingerprint → generate → evaluate → write    │
├─────────────────────────────────────────────────────────┤
│ autotune_manager.cpp — AutotuneManager                  │
│   Coarse search → stability validation → winner select  │
│   Backend-agnostic via CandidateEvaluator callback      │
├─────────────────────────────────────────────────────────┤
│ autotune_runner.cpp — SubprocessRunner                  │
│   Fork/exec miner in --benchmark mode per candidate     │
│   Temp config generation, JSON result parsing           │
│   Crash-safe: timeout + SIGKILL if subprocess hangs     │
├─────────────────────────────────────────────────────────┤
│ autotune_candidates.hpp — Candidate Generation          │
│   AMD: intensity × worksize sweep (CU-aligned)          │
│   NVIDIA: blocks sweep (threads=8 fixed for cn_gpu)     │
├─────────────────────────────────────────────────────────┤
│ autotune_score.hpp — Scoring + Rejection                │
│   Score = hashrate - stability_penalty - error_penalty  │
│   Reject: >5% error rate, >25% CV, zero hashrate       │
├─────────────────────────────────────────────────────────┤
│ autotune_persist.cpp — JSON Persistence                 │
│   autotune.json: device fingerprints + candidate history│
│   Cache lookup by fingerprint compatibility             │
├─────────────────────────────────────────────────────────┤
│ autotune_types.hpp — Shared Types                       │
│   DeviceFingerprint, *Candidate, BenchmarkMetrics, etc. │
└─────────────────────────────────────────────────────────┘
```

## Subprocess Isolation

Each candidate is evaluated by spawning the miner as a child process:

```
Parent (autotune)
  │
  ├── fork() + exec("./n0s-ryo-miner --benchmark 14 --benchwork 20
  │                   --benchmark-json /tmp/result.json
  │                   --amd /tmp/candidate_config.txt
  │                   --noNVIDIA --poolconf /tmp/dummy_pools.txt")
  │
  ├── waitpid() with timeout
  │     ├── Success → parse JSON → score candidate
  │     ├── Exit 134 (SIGABRT/OOM) → mark failed, try next
  │     └── Timeout → SIGKILL → mark failed, try next
  │
  └── Select best, run stability validation, write config
```

**Why subprocess?** GPU backends are loaded via `dlopen()` (shared libraries). Reinitializing CUDA/OpenCL contexts in-process is fragile. Fork/exec gives clean GPU state per candidate, crash isolation, and timeout safety.

## CryptoNight-GPU Kernel Constraints

### NVIDIA (CUDA)

The cn_gpu CUDA kernel uses `__launch_bounds__(128, 8)`:
- **128 threads/block** = 8 thread groups × 16 threads per hash
- **`threads` parameter = 8** (always — this is a kernel constraint, not tunable)
- **`blocks` is the main tunable** — controls total parallelism
- Optimal: Pascal = 7× SM count, Turing+ = 6× SM count
- Per-hash memory: 2 MiB scratchpad + 16 KiB local + 680 B metadata

### AMD (OpenCL)

- **`worksize`** = 8 (most GPUs) or 16 (Vega/newer) — threads per work group
- **`intensity`** = total parallel hashes — bounded by VRAM and CU count
- Optimal: `CU_count × 6 × 8` for CryptoNight-GPU, aligned to worksize
- Per-hash memory: 2 MiB scratchpad + 240 B metadata

## Scoring Model

```
score = hashrate - stability_penalty - error_penalty

stability_penalty:
  CV < 3%  → 0
  CV 3-10% → linear 0-20% of hashrate
  CV > 10% → 50% of hashrate

error_penalty:
  1% invalid shares → 10% of hashrate
  >5% invalid shares → REJECTED (score = 0)
  Any backend errors → 30% penalty
```

Candidates are **rejected outright** if:
- Zero hashrate
- No valid results produced
- Error rate > 5%
- Backend errors > 3
- CV > 25%

## Device Fingerprinting

Results are cached per device fingerprint in `autotune.json`:

```json
{
  "backend": "opencl",
  "gpu_name": "AMD Radeon RX 9070 XT",
  "gpu_architecture": "gfx1201",
  "vram_bytes": 17095983104,
  "compute_units": 32,
  "driver_version": "6.1.0",
  "runtime_version": "OpenCL 2.1",
  "algorithm": "cryptonight_gpu"
}
```

Cache hit requires **exact match** on all fields except `miner_version`. This means:
- Driver update → re-tune
- GPU swap → re-tune
- Same hardware + driver → reuse cached result

## Tuning Modes

| Mode | AMD Candidates | NVIDIA Candidates | Typical Time |
|---|---|---|---|
| Quick | ~9 (3 worksizes × 3 intensities) | 3 (optimal ± 1 block mult) | 2-6 min |
| Balanced | ~10-15 (wider sweep) | 6 (optimal ± 2-3) | 5-10 min |
| Exhaustive | 20+ (full grid) | 11 (mult 2..12) | 15-30 min |

## Files

| File | Lines | Purpose |
|---|---|---|
| `autotune_types.hpp` | 160 | Core types: fingerprint, candidates, metrics, state |
| `autotune_score.hpp` | 100 | Scoring model + guardrail rejection |
| `autotune_candidates.hpp` | 180 | Candidate generation (cn_gpu-aware) |
| `autotune_persist.cpp/hpp` | 320 | JSON serialization via rapidjson |
| `autotune_manager.cpp/hpp` | 450 | Orchestrator: search → validate → select |
| `autotune_runner.cpp/hpp` | 610 | Subprocess evaluation + fingerprinting |
| `autotune_entry.cpp/hpp` | 280 | Entry point + config file writing |
| **Total** | **~2,100** | |
