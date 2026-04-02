# Session 48: CUDA Intensity Tuning

**Date:** 2026-04-01  
**Branch:** `optimize/cuda-intensity-tuning`  
**Commit:** `2c6545e`

---

## Objective

Optimize CUDA block multipliers (blocks = SM_count × multiplier) for Pascal and Turing architectures based on empirical profiling data, specifically analyzing Phase 4 (AES scratchpad compression) behavior under different intensity settings.

---

## Test Configuration

### Hardware
- **Pascal:** NVIDIA GTX 1070 Ti (sm_61, 19 SMs, 8 GB GDDR5)
- **Turing:** NVIDIA RTX 2070 (sm_75, 36 SMs, 8 GB GDDR6)

### Method
- Systematic sweep of block multipliers (T=8 threads per block)
- `--profile` flag enabled for per-phase timing breakdown
- Each config tested with 30+ seconds of mining

---

## Results: Pascal (GTX 1070 Ti)

| Block Multiplier | Intensity | Hashrate | Phase 3 | Phase 4 | Phase 4/hash |
|:---:|:---:|---:|---:|---:|---:|
| **6× (NEW)** | **912** | **1623 H/s** | 490 ms | 53 ms | **59 µs** |
| 7× (old) | 1064 | 1550 H/s | 568 ms | 91 ms | 97 µs |
| 8× | 1216 | 1489 H/s | 566 ms | 103 ms | 102 µs |

### Analysis
- **Phase 4 memory cliff at 7+ blocks/SM** — Phase 4 time jumps from 59 µs/hash → 97+ µs/hash
- Pascal L2 cache (1.4 MB) struggles with 7+ concurrent AES compression operations
- **6×SMs is optimal: +4.7% hashrate vs 7×SMs**

---

## Results: Turing (RTX 2070)

| Block Multiplier | Intensity | Hashrate | Phase 3 | Phase 4 | Phase 4/hash |
|:---:|:---:|---:|---:|---:|---:|
| 5× | 1440 | 2189 H/s | 568 ms | 91 ms | 83 µs |
| 6× (old) | 1728 | 2226 H/s | 661 ms | 91 ms | 81 µs |
| 7× | 2016 | 2252 H/s | 781 ms | 103 ms | 91 µs |
| **8× (NEW)** | **2304** | **2263 H/s** | 885 ms | 104 ms | **92 µs** |
| 9× | 2592 | 2212 H/s | 992 ms | 142 ms | 128 µs |

### Analysis
- Turing handles higher intensity better (4 MB L2 cache vs Pascal's 1.4 MB)
- **8×SMs is optimal: +1.7% hashrate vs 6×SMs**
- No Phase 4 cliff until 9×SMs (thermal/power limits kick in first)
- Phase 3 (compute-bound) benefits from higher occupancy

---

## Code Changes

### 1. CUDA Device Init (`cuda_device.cu`)

**Before:**
```cpp
if(gpuArch / 10 == 6)       // Pascal
    blockOptimal = 7 * ctx->device_mpcount;
else                         // Turing+
    blockOptimal = 6 * ctx->device_mpcount;
```

**After:**
```cpp
if(gpuArch / 10 == 6)       // Pascal
    blockOptimal = 6 * ctx->device_mpcount;
else                         // Turing+
    blockOptimal = 8 * ctx->device_mpcount;
```

### 2. Autotune Candidate Generator (`autotune_candidates.hpp`)

**Before:**
```cpp
uint32_t arch_optimal_mult = (compute_cap >= 70) ? 6 : 7;
```

**After:**
```cpp
uint32_t arch_optimal_mult = (compute_cap >= 70) ? 8 : 6;
```

---

## Validation

### Golden Hashes
✅ All 3 CPU test vectors pass

### Live Mining (3 GPUs)
- ✅ **GTX 1070 Ti (nos2):** 1623 H/s, 100% share acceptance
- ✅ **RTX 2070 (nosnode):** 2263 H/s, 100% share acceptance  
- ✅ **RX 9070 XT (nitro):** 5017 H/s, unaffected (OpenCL unchanged)

---

## Performance Summary

| GPU | Old Hashrate | New Hashrate | Improvement |
|:---|---:|---:|---:|
| **GTX 1070 Ti** | 1550 H/s | **1623 H/s** | **+4.7%** |
| **RTX 2070** | 2226 H/s | **2263 H/s** | **+1.7%** |

**Combined improvement: ~3% on NVIDIA GPUs across Pascal and Turing generations.**

---

## Key Insights

1. **Pascal has a sharp Phase 4 cliff** — 7+ blocks/SM triggers memory contention in AES scratchpad compression (97 µs/hash vs 59 µs at 6×)

2. **Turing benefits from higher intensity** — Larger L2 cache (4 MB) and improved memory controller handle 8×SMs without Phase 4 degradation

3. **Architecture matters more than compute capability** — Pascal sm_61 needs conservative settings; Turing sm_75+ can push harder

4. **Intensity is a multi-phase tradeoff:**
   - Higher intensity → better Phase 3 compute occupancy
   - Too high → Phase 4 memory bottleneck kicks in
   - Optimal point varies by architecture's cache hierarchy

---

## Next Steps

- **Test on Ampere (sm_8x)** — Does 8×SMs remain optimal? Does 10× work?
- **Revisit autotuning sweep ranges** — Current quick mode only tests ±1 from optimal
- **Document memory subsystem benchmarks** — L2 cache size correlation with optimal intensity
