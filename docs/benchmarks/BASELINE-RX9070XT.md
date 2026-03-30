# Baseline: AMD RX 9070 XT (RDNA4) — CryptoNight-GPU

**Date:** 2026-03-30
**Miner:** n0s-ryo-miner v1.0.0 (Session 37)
**Driver:** ROCm 7.2.0 / OpenCL
**Intensity:** 1536
**Worksize:** 8

## Benchmark Mode (no pool overhead)

| Phase | Avg Time (µs) | % of Total |
|-------|---------------|------------|
| Phase 1: Keccak prepare | 127 | 0.0% |
| Phase 2: Scratchpad expand | 39,795 | 11.6% |
| **Phase 3: GPU compute** | **239,331** | **69.8%** |
| Phase 4+5: Implode+final | 63,543 | 18.5% |
| **Total per dispatch** | **342,797** | 100% |

**Effective hashrate: 4,480.8 H/s**
**Benchmark total: 4,505.4 H/s (CV 3.2%)**

## Live Mining (with pool/interleave overhead)

| Phase | Avg Time (µs) | % of Total |
|-------|---------------|------------|
| Phase 1: Keccak prepare | 213 | 0.1% |
| Phase 2: Scratchpad expand | 34,212 | 8.2% |
| **Phase 3: GPU compute** | **310,201** | **74.1%** |
| Phase 4+5: Implode+final | 73,969 | 17.7% |
| **Total per dispatch** | **418,596** | 100% |

**Effective hashrate: 3,669.4 H/s** (pool overhead ~18%)

## Analysis

1. **Phase 3 is the bottleneck** (70-74%) — 49,152 iterations of FP math across 16 cooperative threads
2. **Phase 4+5 is secondary target** (18%) — AES encryption + shuffle over full scratchpad
3. **Phase 2** (8-12%) — Keccak-based scratchpad fill, highly parallelizable
4. **Phase 1** is negligible (<0.1%)

## Optimization Targets

### Phase 3 (highest impact)
- `mem_fence(CLK_LOCAL_MEM_FENCE)` called 4× per iteration (4 × 49,152 = 196,608 barriers)
- Shared memory access pattern: 16 threads read/write 528 bytes
- FP chain: 4 rounds × 8 sub-rounds = 32 divisions per thread per iteration
- Division is the most expensive FP op — `_mm_div_ps` in `fp_round`
- Investigate: can barriers be reduced? Can shared memory layout improve bank conflicts?

### Phase 4+5 (secondary)
- AES over full 2MB scratchpad (sequential reads with stride)
- 8-thread shuffle groups — potential for occupancy tuning
- mix_and_propagate is simple XOR chain — unlikely bottleneck

### Phase 2 (tertiary)
- Keccak permutations to fill scratchpad
- Already uses 64 threads per hash — may be near hardware limit
- Could explore larger workgroup sizes
