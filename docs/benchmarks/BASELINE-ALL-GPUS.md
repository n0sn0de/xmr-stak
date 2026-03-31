# CryptoNight-GPU Profiling Baselines — All 3 GPUs

**Date:** 2026-03-31 (Session 46 — re-baselined)
**Commit:** optimize/phase45-split-profile
**Mode:** `--benchmark 5 --profile` (50 dispatch average)

## Per-Phase Timing Summary

| Phase | RX 9070 XT (RDNA4) | GTX 1070 Ti (Pascal) | RTX 2070 (Turing) |
|-------|--------------------:|---------------------:|-------------------:|
| Phase 1: Keccak | ~100 µs (0.0%) | TBD | TBD |
| Phase 2: Scratchpad | ~40,000 µs (6%) | TBD | TBD |
| **Phase 3: GPU compute** | **~355,000 µs (55%)** | TBD | TBD |
| **Phase 4+5: Implode+final** | **~240,000 µs (38%)** | TBD | TBD |
| **Total** | **~645,000 µs** | TBD | TBD |
| **Hashrate** | **~2,380 H/s** | TBD | TBD |
| Intensity | 1,536 | TBD | TBD |

## Important Note (Session 46)

The Session 38 baselines were recorded under different system conditions (likely different AMD
driver version or kernel). When re-testing at the Session 37 commit, the same ~2,380 H/s was
observed — confirming NO code regression occurred. The environment changed (driver update from
ROCm 7.2.0 to current).

**Key correction:** Phase 4+5 is 32-38% of total time, NOT 18% as previously reported. This
makes Phase 4+5 optimization much more impactful than originally estimated.

## Updated Optimization Priority

1. **Phase 3 FP division** — Still the biggest single phase at 55%, but algorithmically resistant
2. **Phase 4+5 AES implode** — Now 38%, a major optimization target
   - Phase 4 (scratchpad compression): 2 full passes × 16K iterations × 10 AES rounds each
   - Phase 5 (finalize): 16 iterations × 10 AES rounds + Keccak — negligible vs Phase 4
3. **Phase 2 scratchpad expand** — 6%, limited optimization potential

## NVIDIA Baselines (to be updated with split Phase 4/5 timing)

CUDA profiling now separates Phase 4 (implode) from Phase 5 (finalize) timing.
NVIDIA baselines will be collected after deploying the updated profiling code.
