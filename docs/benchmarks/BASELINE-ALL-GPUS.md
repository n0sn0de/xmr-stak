# CryptoNight-GPU Profiling Baselines — All 3 GPUs

**Date:** 2026-03-30 (Session 38)
**Commit:** 008ffee (feature/cuda-profiling)
**Mode:** `--benchmark 10 --profile` (50 dispatch average)

## Per-Phase Timing Summary

| Phase | RX 9070 XT (RDNA4) | GTX 1070 Ti (Pascal) | RTX 2070 (Turing) |
|-------|--------------------:|---------------------:|-------------------:|
| Phase 1: Keccak | 120 µs (0.0%) | ~0 µs (0.0%) | ~0 µs (0.0%) |
| Phase 2: Scratchpad | 41,532 µs (12.0%) | 16,997 µs (2.5%) | 23,983 µs (3.1%) |
| **Phase 3: GPU compute** | **241,069 µs (69.5%)** | **549,414 µs (82.4%)** | **665,811 µs (85.3%)** |
| Phase 4+5: Implode | 64,201 µs (18.5%) | 100,656 µs (15.1%) | 91,047 µs (11.7%) |
| **Total** | **346,923 µs** | **667,067 µs** | **780,842 µs** |
| **Hashrate** | **4,427.5 H/s** | **1,595.0 H/s** | **2,213.0 H/s** |
| Intensity | 1,536 | 1,064 | 1,728 |

## Key Insights

1. **Phase 3 dominates everywhere** — 69-85% of total compute
2. **NVIDIA spends more % in Phase 3** than AMD (82-85% vs 69.5%)
3. **AMD RDNA4 is ~2x faster per-dispatch** than Pascal despite larger intensity
4. **Phase 2 (Keccak expand)** is relatively cheaper on NVIDIA — their scalar Keccak is fast
5. **Phase 4+5 (AES implode)** is a secondary target — 11-18% across all GPUs
6. **RTX 2070 slower than expected** — 2213 H/s at 1728 intensity suggests Phase 3 is the bottleneck
   - RTX 2070 has more CUDA cores than GTX 1070 Ti but Phase 3 is FP-division bound
   - 85.3% in Phase 3 = almost entirely division-limited

## Optimization Priority

1. **Phase 3 FP division** — 32 divisions per sub-round × 8 sub-rounds × 4 rounds = 1024 divisions per thread per iteration × 49,152 iterations
2. **Phase 3 memory barriers** — 4 `mem_fence`/`__syncwarp` per iteration = 196,608 barriers
3. **Phase 3 shared memory bank conflicts** — 16 threads accessing shared memory patterns
4. **Phase 4+5 AES** — Secondary target, but 91-100K µs is non-trivial
