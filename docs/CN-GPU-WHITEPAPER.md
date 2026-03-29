# CryptoNight-GPU Algorithm Specification

**A Deep Dive into the Design, Architecture, and Implementation**

*n0s-ryo-miner Technical Documentation*

---

## Table of Contents

1. [Overview](#overview)
2. [Design Philosophy](#design-philosophy)
3. [Algorithm Parameters](#algorithm-parameters)
4. [Pipeline Architecture](#pipeline-architecture)
5. [Phase 1: State Initialization (Prepare)](#phase-1-state-initialization-prepare)
6. [Phase 2: Scratchpad Expansion (Explode)](#phase-2-scratchpad-expansion-explode)
7. [Phase 3: Main Loop (GPU Compute)](#phase-3-main-loop-gpu-compute)
8. [Phase 4: Scratchpad Compression (Implode)](#phase-4-scratchpad-compression-implode)
9. [Phase 5: Final Hash](#phase-5-final-hash)
10. [Thread Topology](#thread-topology)
11. [Floating-Point Math Core](#floating-point-math-core)
12. [Memory Access Patterns](#memory-access-patterns)
13. [ABI Contract: The Sacred Enum](#abi-contract-the-sacred-enum)
14. [Implementation Comparison: CUDA vs OpenCL](#implementation-comparison-cuda-vs-opencl)
15. [GPU Tuning Parameters](#gpu-tuning-parameters)
16. [Cryptographic Hash Functions Used](#cryptographic-hash-functions-used)

---

## Overview

CryptoNight-GPU (`cn/gpu`, algorithm ID `13`) is a proof-of-work hash function designed by the RYO Currency team. It is a variant of the CryptoNight family specifically engineered to be **GPU-friendly** — meaning it runs efficiently on massively parallel GPU architectures while being intentionally slow on CPUs, FPGAs, and ASICs.

Unlike classic CryptoNight (which uses memory-hard sequential read-modify-write loops that favor CPUs), CryptoNight-GPU replaces the main loop with a **cooperative floating-point computation** that requires 16 GPU threads working together with shared memory synchronization. This creates a workload that maps naturally to GPU warps/wavefronts but is pathologically inefficient on sequential processors.

### What Makes It "GPU-Native"

1. **16-thread cooperative groups** — Each hash requires 16 threads working in lockstep, naturally mapping to GPU warp/wavefront subdivisions
2. **Floating-point arithmetic** — The main loop is dominated by `float32` multiply/add/divide operations, utilizing GPU FP32 ALUs that sit idle in integer-heavy algorithms
3. **Shared memory communication** — Threads exchange data through GPU shared/local memory (fast scratchpad), not global memory
4. **Data-dependent scratchpad access** — Memory addresses are derived from computation results, creating unpredictable access patterns that defeat simple caching strategies
5. **FMA dependency chains** — Carefully constructed to prevent compiler optimization while maintaining GPU throughput

---

## Design Philosophy

```
┌─────────────────────────────────────────────────────────────┐
│                  CryptoNight-GPU Design Goals                │
├─────────────────────────────────────────────────────────────┤
│  ✅ Efficient on GPUs (NVIDIA & AMD)                        │
│  ✅ 2 MiB memory per hash (fits in GPU global memory)       │
│  ✅ Floating-point heavy (uses GPU FP32 units)              │
│  ✅ Cooperative threading (16 threads per hash)              │
│  ❌ Slow on CPUs (cooperative FP math is serialized)        │
│  ❌ Hard for FPGAs (floating-point is expensive in gates)   │
│  ❌ Hard for ASICs (same FP problem + memory requirement)   │
└─────────────────────────────────────────────────────────────┘
```

The key insight: GPUs have thousands of FP32 ALUs and fast shared memory. By making the proof-of-work depend on cooperative floating-point math with shared memory synchronization, you create an algorithm where GPUs have a natural, massive advantage.

---

## Algorithm Parameters

| Parameter | Value | Meaning |
|---|---|---|
| `MEMORY` | 2,097,152 (2 MiB) | Scratchpad size per hash |
| `ITERATIONS` | 0xC000 (49,152) | Main loop iteration count |
| `MASK` | 0x1FFFC0 | Scratchpad address mask (64-byte aligned, within 2 MiB) |
| Threads per hash | 16 | Cooperative group size |
| Thread sub-groups | 4 groups of 4 | Hierarchical structure within the 16-thread group |
| FP precision | IEEE 754 float32 | Single-precision floating point |
| Algorithm ID | 13 | ABI contract value (see [ABI section](#abi-contract-the-sacred-enum)) |

### Memory Layout

```
Scratchpad (2 MiB per hash):
┌────────────────────────────────────────────────────┐
│ 128 KiB segments, filled by Keccak-1600 expansion  │
│                                                     │
│ Total: 2,097,152 bytes = 131,072 × 16-byte blocks  │
│                                                     │
│ Accessed in 64-byte chunks (4 × 16-byte int4)      │
│ Address masked to 0x1FFFC0 (64-byte aligned)        │
└────────────────────────────────────────────────────┘

State (200 bytes = 50 × uint32 per hash):
┌────────────────────────────────────────────────────┐
│ [0..7]    Keccak state words (initial + absorbed)   │
│ [8..15]   AES key material (key1 derived)           │
│ [16..49]  Extended state for AES key2 + text        │
└────────────────────────────────────────────────────┘
```

---

## Pipeline Architecture

CryptoNight-GPU processes each hash through a 5-phase pipeline. Each phase is a separate GPU kernel launch:

```
Input (block header + nonce)
          │
          ▼
┌─────────────────────┐
│  PHASE 1: Prepare   │  Keccak-1600 hash of input → 200-byte state
│  (cryptonight_      │  AES-256 key expansion (key1, key2)
│   extra_gpu_prepare)│  XOR state halves → initial a, b values
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│  PHASE 2: Explode   │  Keccak-1600 permutations fill 2 MiB scratchpad
│  (cn_explode_gpu)   │  512 bytes per Keccak squeeze (3 permutations)
│                     │  ~4096 iterations to fill 2 MiB
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│  PHASE 3: Main Loop │  ★ THE GPU-SPECIFIC PART ★
│  (cryptonight_core_ │  49,152 iterations of:
│   gpu_phase2_gpu)   │    Read scratchpad → FP math → Write scratchpad
│                     │  16 threads cooperate via shared memory
│                     │  Floating-point multiply/add/divide chains
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│  PHASE 4: Implode   │  AES encryption over scratchpad data
│  (cryptonight_core_ │  8-thread shuffle groups for mix_and_propagate
│   gpu_phase3)       │  Two full passes over scratchpad
│                     │  XOR accumulated result back into state
└─────────┬───────────┘
          │
          ▼
┌─────────────────────┐
│  PHASE 5: Finalize  │  16 rounds of AES + mix_and_propagate on state
│  (cryptonight_      │  Final Keccak-1600 permutation
│   extra_gpu_final)  │  Compare hash against target difficulty
└─────────┬───────────┘
          │
          ▼
    Hash < Target? ──yes──► Share accepted!
          │no
          ▼
    Try next nonce
```

---

## Phase 1: State Initialization (Prepare)

**Kernel:** `cryptonight_extra_gpu_prepare`
**Threads:** 1 per hash (128 threads/block)

Each hash gets a unique nonce injected into the input block header at byte offset 39.

```
Input (76-84 bytes) + nonce
         │
         ▼
    Keccak-1600
         │
         ▼
200-byte state (50 × uint32)
         │
    ┌────┴────┐
    ▼         ▼
AES key1   AES key2
(from state  (from state
 [0..7])     [8..15])
    │         │
    ▼         ▼
  a = state[0..3] XOR state[8..11]    (initial accumulator)
  b = state[4..7] XOR state[12..15]   (initial accumulator)
```

The AES key expansion follows standard AES-256 key schedule (10 rounds, 40 × uint32 expanded key).

---

## Phase 2: Scratchpad Expansion (Explode)

**Kernel:** `cn_explode_gpu`
**Threads:** 128 per hash (parallel fill)

The 2 MiB scratchpad is filled using repeated Keccak-1600 permutations:

```
For each 512-byte block i in scratchpad:
    hash[0] = state[0] XOR i     ← block index mixed in
    hash[1..24] = state[1..24]   ← rest of state copied
    
    Keccak-f1600(hash)  →  output 160 bytes (20 uint64s)
    Keccak-f1600(hash)  →  output 176 bytes (22 uint64s)  
    Keccak-f1600(hash)  →  output 176 bytes (22 uint64s)
                           ─────────────────────────────
                           Total: 512 bytes per block
```

This produces `2 MiB / 512 = 4096` blocks. With 128 threads per hash, each thread fills ~32 blocks. The Keccak permutation is cryptographically strong, so the scratchpad content is pseudorandom and unpredictable.

---

## Phase 3: Main Loop (GPU Compute)

**Kernel:** `cryptonight_core_gpu_phase2_gpu`
**Threads:** 16 per hash (cooperative group)
**Shared memory:** 33 × 16 bytes per hash (528 bytes)

This is the heart of CryptoNight-GPU — the phase that makes it GPU-native.

### Thread Organization

```
16 threads per hash:
┌──────────────────────────────────────────┐
│  Thread 0   Thread 1   Thread 2   Thread 3   │  ← Group 0 (tidd=0)
│  Thread 4   Thread 5   Thread 6   Thread 7   │  ← Group 1 (tidd=1)
│  Thread 8   Thread 9   Thread 10  Thread 11  │  ← Group 2 (tidd=2)
│  Thread 12  Thread 13  Thread 14  Thread 15  │  ← Group 3 (tidd=3)
└──────────────────────────────────────────┘

Each thread has:
  tid  = thread index [0..15]
  tidd = tid / 4  (group index [0..3])
  tidm = tid % 4  (position within group [0..3])
```

### Main Loop Iteration (×49,152)

Each iteration proceeds through these steps:

#### Step 1: Load from Scratchpad
```
Address = state_index & MASK    (data-dependent, 64-byte aligned)
Each of 4 groups loads one 16-byte int4 from scratchpad[address + group*16]
Thread within group loads its uint32 component
→ Stored into shared memory: smem->out[0..15]
```

#### Step 2: Floating-Point Computation (single_comupte_wrap)
```
Each thread:
  1. Looks up 4 input int4 values from shared memory using look[tid] table
  2. Converts int4 → float4 (integer to floating-point)
  3. Runs single_compute():
     - 4 rounds of round_compute()
     - Each round: 8 sub_rounds of floating-point math
     - Total: 32 sub_rounds per thread per iteration
  4. Converts result float4 → int4
  5. Optionally rotates output by tidm bytes
→ Result stored in smem->out[tid] and smem->va[tid]
```

#### Step 3: XOR Reduction + Scratchpad Write
```
Within each group of 4 threads:
  outXor = smem->out[base] XOR smem->out[base+4] XOR smem->out[base+8] XOR smem->out[base+12]
  
Write back: scratchpad[address + group*16] = outXor XOR original_value
```

#### Step 4: Floating-Point Accumulation
```
Two rounds of cross-group reduction on smem->va[] values:
  va_tmp = va[base] + va[base+4] + va[base+8] + va[base+12]

After reduction:
  vs = abs(smem->va[0])     ← single float4 value
  out2 = smem->out[0..3] XOR'd together
  
  vs_scaled = vs * 16777216.0f
  out2 = out2 XOR int(vs_scaled)
  
  vs = vs / 64.0f           ← normalize to [0, 1) range
  
  state_index = out2.x XOR out2.y XOR out2.z XOR out2.w
  ↑ This becomes the next iteration's scratchpad address
```

### The `look` Table

The `look[16][4]` constant table determines which 4 of the 16 shared memory slots each thread reads as input to its floating-point computation. This creates a complex cross-thread data dependency pattern:

```
Thread 0:  reads slots [0, 1, 2, 3]
Thread 1:  reads slots [0, 2, 3, 1]
Thread 2:  reads slots [0, 3, 1, 2]
Thread 3:  reads slots [0, 3, 2, 1]
Thread 4:  reads slots [1, 0, 2, 3]
Thread 5:  reads slots [1, 2, 3, 0]
...
Thread 15: reads slots [3, 0, 2, 1]
```

This ensures every thread's output depends on multiple other threads' outputs from the previous iteration, creating a tight web of inter-thread dependencies that requires shared memory synchronization.

### The `ccnt` Table

16 unique floating-point constants, one per thread, used as the initial `c` value in the floating-point computation. These are carefully chosen values in the range [1.25, 1.47]:

```
1.34375, 1.28125, 1.359375, 1.3671875,
1.4296875, 1.3984375, 1.3828125, 1.3046875,
1.4140625, 1.2734375, 1.2578125, 1.2890625,
1.3203125, 1.3515625, 1.3359375, 1.4609375
```

These are all exact in IEEE 754 float32 (they're multiples of 2^-10 = 1/1024), ensuring deterministic results across all GPU architectures.

---

## Floating-Point Math Core

The innermost computation is the `sub_round` function, called 32 times per thread per main loop iteration:

```
sub_round(n0, n1, n2, n3, rnd_c, &n, &d, &c):
    n1 = n1 + c                          ← add accumulator
    nn = n0 * c                           ← multiply by accumulator
    nn = n1 * (nn * nn)                   ← cube-like operation
    nn = fma_break(nn)                    ← break FMA chain (see below)
    n += nn                               ← accumulate numerator
    
    n3 = n3 - c                           ← subtract accumulator
    dd = n2 * c                           ← multiply by accumulator
    dd = n3 * (dd * dd)                   ← cube-like operation
    dd = fma_break(dd)                    ← break FMA chain
    d += dd                               ← accumulate denominator
    
    c = c + rnd_c                         ← evolve accumulator
    c = c + 0.734375                      ← constant offset
    r = nn + dd                           ← combine
    r = (r & 0x807FFFFF) | 0x40000000    ← force exponent to [2.0, 4.0)
    c = c + r                             ← feedback into accumulator
```

### `fma_break` — Preventing Hardware Optimization

```
fma_break(x):
    x = x & 0xFEFFFFFF    ← clear bit 24 of exponent
    x = x | 0x00800000    ← set bit 23 of exponent
    // Result: exponent has pattern ?????01 in low 2 bits
    // This forces the value into a specific exponent range,
    // breaking FMA dependency chains that hardware might optimize away
```

This is a critical anti-optimization trick. Without it, the compiler/GPU could fuse multiple multiply-add operations into single FMA instructions, reducing the computational work. By manipulating the IEEE 754 exponent bits directly, the algorithm ensures each operation must execute independently.

### Division Safety

```
// Before dividing n/d:
d = d & 0xFF7FFFFF    ← clear sign bit of exponent
d = d | 0x40000000    ← force exponent ≥ 2.0
// Guarantees |d| > 2.0, preventing division by zero or overflow
```

### FLOP Count per Hash

```
Per sub_round:     ~9 FP operations (mul, add, sub, bitwise)
Per round_compute: 8 sub_rounds + 2 ops = 74 FP ops
Per single_compute: 4 round_computes + 3 ops = ~300 FP ops
Per thread per iteration: ~300 FP ops
Per hash (16 threads × 49,152 iterations): ~235 million FP ops
```

---

## Phase 4: Scratchpad Compression (Implode)

**Kernel:** `cryptonight_core_gpu_phase3`
**Threads:** 8 per hash (shuffle groups)

This phase reads the entire scratchpad back and compresses it into the state using AES encryption:

```
For each 32-byte chunk in scratchpad (2 full passes):
    text[0..3] ^= scratchpad[offset + sub*4 .. offset + sub*4 + 3]
    text = AES_pseudo_round(text, key2)
    
    // Shuffle: each thread sends its text to neighbor (thread+1) % 8
    // and XORs the received value
    tmp = shuffle<8>(text, (subv+1) & 7)
    text ^= tmp
```

The 8-thread shuffle implements the `mix_and_propagate` operation — data flows between neighboring threads in a ring pattern, ensuring all 8 sub-sections of the state mix together.

After processing the entire scratchpad (twice), the compressed result is written back to the state.

---

## Phase 5: Final Hash

**Kernel:** `cryptonight_extra_gpu_final`
**Threads:** 1 per hash (128 threads/block)

The finalization applies 16 more rounds of AES + mix_and_propagate to the state, then runs a final Keccak-1600 permutation:

```
For i in 0..15:
    For each of 8 state blocks [4..11]:
        block = AES_pseudo_round(block, key2)
    mix_and_propagate(state[4..11])

Keccak-f1600(state)

// Check against target
if state[3] < target:
    → Valid share! Report nonce.
```

### `mix_and_propagate`

```
save = state[0]
for t in 0..6:
    state[t] ^= state[t+1]
state[7] ^= save
```

This is a simple XOR-shift propagation that ensures changes in any block affect all subsequent blocks after multiple rounds.

---

## Thread Topology

### CUDA Thread Mapping

```
Grid:    device_blocks blocks
Block:   device_threads × 16 threads (for Phase 3)

Phase 2 (Explode):
  Grid:  intensity (blocks × threads per block)
  Block: 128 threads
  Each block fills one hash's scratchpad

Phase 3 (Main Loop):
  Grid:  device_blocks
  Block: device_threads × 16
  Shared memory: device_threads × 528 bytes
  16 threads per hash = 1 cooperative group

Phase 4 (Implode):
  Grid:  gridSizePhase3
  Block: block8 (device_threads × 8)
  8 threads per hash with warp shuffle
```

### Shared Memory Layout (Phase 3)

```c
struct SharedMemChunk {
    __m128i out[16];    // 256 bytes — computation outputs
    __m128  va[17];     // 272 bytes — floating-point accumulators
};                      // 528 bytes total per hash
```

One `SharedMemChunk` per hash within each block. Multiple hashes share a block.

---

## Memory Access Patterns

### Scratchpad Reads (Phase 3)

```
Address = (state_index & 0x1FFFC0)  ← 64-byte aligned within 2 MiB

state_index evolves based on computation:
  state_index = XOR of all 4 words in the reduced output

This creates a RANDOM walk through the scratchpad:
  - Not sequential (no prefetching benefit)
  - Not periodic (no cache line reuse pattern)
  - Data-dependent (address depends on previous computation)
```

This pattern is designed to defeat L1/L2 caching strategies. Each hash's 2 MiB scratchpad is much larger than per-SM L1 cache, and the random access pattern ensures cache lines are evicted before reuse.

### Global vs Shared Memory

| Phase | Memory Type | Access Pattern |
|---|---|---|
| Explode | Global (write) | Sequential, coalesced |
| Main Loop — read | Global (read) | Random, 64-byte aligned |
| Main Loop — compute | Shared (R/W) | All-to-all within 16 threads |
| Main Loop — write | Global (write) | Random, same address as read |
| Implode | Global (read) | Sequential with stride |

---

## ABI Contract: The Sacred Enum

The algorithm ID `13` is a critical cross-language ABI contract:

```cpp
// C++ (cryptonight.hpp)
enum xmrstak_algo_id {
    invalid_algo = 0,
    cryptonight_gpu = 13  // ← MUST be 13
};

// OpenCL (cryptonight.cl)
#define cryptonight_gpu 13  // ← MUST match C++

// CUDA (compiled with)
-DALGO=13                   // ← MUST match both
```

**Why it matters:** OpenCL kernels are compiled at runtime with `#define ALGO 13`. CUDA kernels use template specialization with `cryptonight_gpu` as a compile-time enum value. If these three values don't match, the miner silently produces wrong hashes.

This value of `13` is inherited from the original xmr-stak multi-algorithm enum and must never change without simultaneously updating all three locations.

---

## Implementation Comparison: CUDA vs OpenCL

| Aspect | CUDA | OpenCL |
|---|---|---|
| Vector type | Custom `__m128i`/`__m128` structs | Native `int4`/`float4` |
| Int↔float conversion | `int2float()` / `__int2float_rn()` | `convert_float4_rte()` |
| Bit reinterpret | `float_as_int()` / `__float_as_int()` | `as_int()` / `as_float()` |
| Shared memory | `__shared__` + `__syncwarp()` | `__local` + `mem_fence()` |
| Memory loads | Inline PTX `ld.global.cg` (< sm_70) | Standard loads |
| Synchronization | `__syncwarp()` (CUDA 9+) / `__syncthreads()` | `mem_fence(CLK_LOCAL_MEM_FENCE)` |
| Byte rotation | Custom `_mm_alignr_epi8` | Custom `_mm_alignr_epi8` |
| Bfactor (kernel splitting) | Supported (multi-launch) | Not used (single launch) |

### CUDA-Specific: Bfactor

CUDA supports splitting the main loop into multiple kernel launches (`bfactor`). This prevents the OS from killing long-running kernels on GPUs also driving a display:

```
bfactor = 0: 1 launch, all 49152 iterations
bfactor = 1: 2 launches, 24576 iterations each
bfactor = N: 2^N launches
```

Between launches, intermediate state (`vs`, `s`) is saved to global memory and restored on the next launch.

### CUDA-Specific: Architecture Optimizations

```
sm_60 (Pascal):  Use inline PTX for global loads (ld.global.cg)
sm_70+ (Volta+): Use direct pointer dereference (hardware handles caching)

sm_30+ (Kepler+): Use warp shuffle for Phase 4
sm_90+ (Hopper):  Use __shfl_sync with __activemask()
```

---

## GPU Tuning Parameters

### NVIDIA (`nvidia.txt`)

| Parameter | Default | Description |
|---|---|---|
| `threads` | 8 | Hashes per block (auto-tuned based on memory) |
| `blocks` | 8 × SMs | Thread blocks (auto-tuned by architecture) |
| `bfactor` | 0-6 | Kernel split factor (higher = more responsive, lower hashrate) |
| `bsleep` | 0 | Sleep between split kernels (ms) |
| `affine_to_cpu` | false | Pin CPU thread to specific core |

**Auto-tuning logic:**
- Pascal (sm_6x): 7 blocks per SM
- Turing+ (sm_7x+): 6 blocks per SM
- Low-end (≤6 SMs): bfactor increased by 2 automatically
- Memory-limited: threads reduced to fit available VRAM

### AMD (`amd.txt`)

| Parameter | Default | Description |
|---|---|---|
| `intensity` | auto | Work items per GPU |
| `worksize` | 16 | Work group size (must be multiple of 16) |
| `strided_index` | 0 | Memory layout (0 = contiguous per hash) |
| `comp_mode` | 1 | Compatibility mode (bounds checking) |

---

## Cryptographic Hash Functions Used

| Function | Where Used | Purpose |
|---|---|---|
| **Keccak-1600** | Phase 1 (init), Phase 2 (expand), Phase 5 (finalize) | Core hash function, state permutation |
| **AES-256** | Phase 1 (key expansion), Phase 4 (compress), Phase 5 (finalize) | Key schedule, pseudo-round encryption |
| **Blake-256** | Phase 5 (finalize, unused in cn_gpu) | Legacy — present in code but not called by cn_gpu |
| **Groestl-256** | Phase 5 (finalize, unused in cn_gpu) | Legacy — present in code but not called by cn_gpu |
| **JH-256** | Phase 5 (finalize, unused in cn_gpu) | Legacy — present in code but not called by cn_gpu |
| **Skein-256** | Phase 5 (finalize, unused in cn_gpu) | Legacy — present in code but not called by cn_gpu |

**Note:** Classic CryptoNight uses Blake/Groestl/JH/Skein as a final hash selector (based on state bits). CryptoNight-GPU bypasses this — it always outputs directly from Keccak. The other hash functions remain in the codebase as the OpenCL host code still creates their kernel objects, but they execute only empty stubs for `cn_gpu`.

---

## Appendix: Constants Reference

### Scratchpad

```
MEMORY     = 0x200000  = 2,097,152 bytes (2 MiB)
MASK       = 0x1FFFC0  = 2,097,088 (64-byte aligned mask)
ITERATIONS = 0xC000    = 49,152
```

### IEEE 754 Bit Manipulation

```
0x807FFFFF  — sign bit + mantissa (clear exponent)
0x40000000  — exponent = 2^1, forces value into [2.0, 4.0)
0xFEFFFFFF  — clear bit 24 (fma_break)
0x00800000  — set bit 23 (fma_break)
0xFF7FFFFF  — clear sign bit (absolute value of exponent)
536870880.0 — 2^29 - 2^5, used to scale float→int conversion
16777216.0  — 2^24, used for final float→int accumulation
```

---

*This document describes the CryptoNight-GPU algorithm as implemented in n0s-ryo-miner. For the original specification, see the RYO Currency documentation.*
