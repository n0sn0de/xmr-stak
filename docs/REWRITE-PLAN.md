# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Planning — not yet started*

---

## Goal

Take the current battle-tested CryptoNight-GPU implementation and rewrite the backend code into clean, modern, idiomatic C++17. The algorithm and its math must remain bit-exact — we're changing how the code is organized and expressed, not what it computes.

**Non-goals:** Changing the algorithm, adding new features, or modifying the mining protocol. This is purely a structural and code quality rewrite.

---

## Principles

1. **Bit-exact output** — Every rewritten function must produce identical results to the original. This is non-negotiable. We verify with hash comparison, not just "shares accepted."
2. **One module at a time** — Rewrite and verify a single compilation unit before touching the next. Never have two things broken simultaneously.
3. **Test-driven** — Write validation harnesses that capture original output, then verify rewritten code matches. Binary diff of scratchpad contents at each phase.
4. **No magic** — Every function, constant, and parameter gets a clear name and a comment explaining *why*. The original code has cryptic names (`smem->va`, `ccnt`, `look`, `tidd`, `tidm`) that deserve real identifiers.
5. **Modern idioms** — `std::span`, `std::array`, `constexpr`, `[[nodiscard]]`, scoped enums, RAII. No raw `new`/`delete`, no C-style casts, no `memcpy` where structured copies work.

---

## Architecture Vision

### Current State (Inherited xmr-stak)

```
xmrstak/
├── backend/
│   ├── amd/          ← AMD OpenCL backend (gpu.cpp, minethd, jconf, autoAdjust)
│   │   └── amd_gpu/
│   │       └── opencl/  ← 7 .cl kernel files (string literals)
│   ├── nvidia/       ← NVIDIA CUDA backend (minethd, jconf, autoAdjust)
│   │   └── nvcc_code/   ← 10 .cu/.hpp files
│   ├── cpu/          ← Shared crypto library (NOT cpu mining)
│   │   └── crypto/      ← Hash functions, AES, cn_gpu CPU impl
│   ├── cryptonight.hpp  ← Algorithm constants + enum (shared)
│   ├── globalStates.*   ← Global mutable state (job queue)
│   ├── backendConnector.*  ← Dispatches work to backends
│   └── miner_work.hpp  ← Work unit definition
├── net/              ← Pool connection (stratum)
├── http/             ← HTTP API
├── misc/             ← Console, config, telemetry, environment
└── cli/              ← Main entry point
```

### Target State

```
n0s/
├── algorithm/
│   ├── cn_gpu.hpp          ← Algorithm constants, types, parameters
│   ├── keccak.hpp          ← Keccak-1600 (shared between all backends)
│   └── aes.hpp             ← AES key expansion + pseudo-rounds (shared)
│
├── gpu/
│   ├── common/
│   │   ├── device.hpp      ← Abstract GPU device interface
│   │   ├── kernel.hpp      ← Abstract kernel launch interface
│   │   └── auto_tune.hpp   ← Shared auto-tuning logic
│   │
│   ├── cuda/
│   │   ├── cuda_device.cpp     ← NVIDIA device enumeration + init
│   │   ├── cuda_kernels.cu     ← All CUDA kernels (single file)
│   │   ├── cuda_backend.cpp    ← CUDA mining thread implementation
│   │   └── cuda_config.cpp     ← nvidia.txt parsing
│   │
│   └── opencl/
│       ├── ocl_device.cpp      ← AMD/OpenCL device enumeration + init
│       ├── ocl_kernels.cl      ← All OpenCL kernels (single file)
│       ├── ocl_backend.cpp     ← OpenCL mining thread implementation
│       └── ocl_config.cpp      ← amd.txt parsing
│
├── pool/
│   ├── stratum.cpp         ← Stratum protocol (JSON-RPC)
│   ├── connection.cpp      ← Socket management, TLS
│   └── job.hpp             ← Work unit / job definition
│
├── monitor/
│   ├── http_api.cpp        ← HTTP JSON API
│   ├── telemetry.cpp       ← Hashrate + share tracking
│   └── console.cpp         ← Console output
│
├── config/
│   ├── config.cpp          ← Main config (pools.txt)
│   ├── params.hpp          ← CLI parameters
│   └── environment.cpp     ← Singleton management
│
└── main.cpp                ← Entry point
```

Key changes:
- **Flat, logical grouping** instead of the current deep nesting
- **Algorithm separated from backends** — `cn_gpu.hpp` defines constants and types, backends implement them
- **No more `xmrstak` namespace pollution** — use `n0s::` or `ryo::`
- **Single kernel file per backend** — the current split across 10+ CUDA files is unnecessary for a single algorithm
- **Shared auto-tuning logic** — extracted from backend-specific code into common module

---

## Phased Approach

### Phase R1: Validation Harness

Before touching any code, build a test harness that captures the exact output of every phase:

1. **Hash capture tool** — Given a fixed input + nonce, dump:
   - Phase 1 output: 200-byte state, key1, key2, a, b
   - Phase 2 output: First and last 512 bytes of scratchpad
   - Phase 3 output: State index `s` and `vs` after N iterations
   - Phase 4 output: State after AES compression
   - Phase 5 output: Final 32-byte hash

2. **Bit-exact comparison** — Compare old vs new output byte-by-byte. Any difference = bug.

3. **Performance baseline** — Record hashrate per GPU before any changes.

### Phase R2: Algorithm Module

Extract algorithm constants and types into a clean standalone header:

```cpp
namespace n0s::cn_gpu {
    constexpr size_t SCRATCHPAD_SIZE = 2 * 1024 * 1024;  // 2 MiB
    constexpr uint32_t ITERATIONS = 0xC000;               // 49,152
    constexpr uint32_t ADDRESS_MASK = 0x1FFFC0;           // 64-byte aligned
    constexpr uint32_t THREADS_PER_HASH = 16;
    constexpr uint32_t GROUPS_PER_HASH = 4;
    
    // The look table: cross-thread data dependency pattern
    constexpr std::array<std::array<uint32_t, 4>, 16> SHUFFLE_PATTERN = {{ ... }};
    
    // Per-thread initial constants (exact IEEE 754 float32)
    constexpr std::array<float, 16> THREAD_CONSTANTS = {{ ... }};
}
```

### Phase R3: Shared Crypto

Clean up Keccak and AES implementations:
- Remove multi-algorithm dispatch (only cn_gpu remains)
- Add `constexpr` where possible
- Replace C-style arrays with `std::array`
- Add proper `[[nodiscard]]` annotations
- Document each function with its role in the pipeline

### Phase R4: CUDA Backend Rewrite

The biggest chunk. Rewrite in this order:

1. **cuda_device** — Device enumeration, capability checking, memory allocation
2. **cuda_kernels** — Consolidate all kernels into one file with clear naming:
   - `kernel_prepare()` → Phase 1
   - `kernel_expand_scratchpad()` → Phase 2  
   - `kernel_gpu_compute()` → Phase 3
   - `kernel_compress_scratchpad()` → Phase 4
   - `kernel_finalize()` → Phase 5
3. **cuda_backend** — Mining thread loop, job management

**Critical:** CUDA kernels can't use most C++ features (no exceptions, limited STL). The rewrite focuses on naming, organization, and documentation — not C++ modernization of device code.

### Phase R5: OpenCL Backend Rewrite

Similar to CUDA but with OpenCL specifics:
- OpenCL kernels are strings compiled at runtime
- Can reorganize the string assembly but the kernel language is fixed
- Focus on the host-side code: device init, kernel compilation, parameter passing

### Phase R6: Pool/Network Layer

Clean up stratum implementation:
- Replace raw socket management with modern patterns
- Proper error handling (no more silent failures)
- Clear separation of JSON-RPC protocol from transport

### Phase R7: Configuration and CLI

- Modern CLI parsing (consider `CLI11` library or simple hand-rolled)
- Typed configuration (no more raw JSON traversal everywhere)
- Validation at parse time, not at use time

---

## Naming Conventions

| Current | Proposed | Why |
|---|---|---|
| `single_comupte` | `compute_fp_chain` | Fix typo, describe purpose |
| `single_comupte_wrap` | `compute_fp_chain_rotated` | Describes the rotation |
| `sub_round` | `fp_sub_round` | Prefix for floating-point scope |
| `round_compute` | `fp_round` | Shorter, clear |
| `smem->va` | `shared.fp_accumulators` | Descriptive |
| `smem->out` | `shared.computation_output` | Descriptive |
| `ccnt[16]` | `THREAD_CONSTANTS[16]` | Self-documenting |
| `look[16][4]` | `SHUFFLE_PATTERN[16][4]` | Describes function |
| `tidd` / `tidm` | `group_index` / `lane_index` | Standard GPU terminology |
| `spad` / `lpad` | `state_buffer` / `scratchpad` | Obvious meaning |
| `vs` | `fp_accumulator` | What it actually is |
| `cn_explode_gpu` | `kernel_expand_scratchpad` | Action + target |
| `cryptonight_core_gpu_phase2_gpu` | `kernel_gpu_compute` | Drop the stuttering |

---

## Risk Areas

| Area | Risk | Mitigation |
|---|---|---|
| IEEE 754 determinism | **HIGH** — float ops must be bit-exact across all GPUs | Validation harness with known-good hashes |
| Shared memory layout | **HIGH** — padding/alignment differences = wrong results | Test on Pascal, Turing, and AMD simultaneously |
| ABI enum value | **CRITICAL** — changing `13` breaks everything | Never change it. Wrap in `static_assert`. |
| OpenCL string kernels | **MEDIUM** — can't easily refactor runtime-compiled code | Keep OpenCL kernel strings separate, focus on host code |
| Performance regression | **MEDIUM** — cleaner code might lose micro-optimizations | Benchmark before/after each phase |
| CUDA inline PTX | **LOW** — architecture-specific asm is delicate | Keep PTX paths, just organize better |

---

## Timeline Estimate

| Phase | Effort | Dependency |
|---|---|---|
| R1: Validation harness | 4-8 hours | None (do first) |
| R2: Algorithm module | 2-4 hours | R1 |
| R3: Shared crypto | 4-8 hours | R1, R2 |
| R4: CUDA backend | 16-24 hours | R1, R2, R3 |
| R5: OpenCL backend | 12-16 hours | R1, R2, R3 |
| R6: Pool/network | 8-12 hours | Independent |
| R7: Config/CLI | 4-8 hours | Independent |

**Total: ~50-80 hours of focused work**

R1 is the most critical — without the validation harness, we're flying blind. Everything else can be parallelized once R1 is solid.

---

## Success Criteria

- [ ] All hashes bit-exact with original implementation
- [ ] Hashrate within 1% of original on all tested GPUs
- [ ] Zero share rejections in 1-hour test runs
- [ ] All `constexpr` where possible
- [ ] No raw `new`/`delete`
- [ ] No global mutable state outside `main()`
- [ ] Every function documented with its pipeline role
- [ ] Single-command build (`cmake .. && make`)
- [ ] Clean compiler output (zero warnings at `-Wall -Wextra`)

---

*This plan respects the complexity of GPU kernel code. We're not rewriting for the sake of rewriting — we're making the code ours so we can reason about it, optimize it, and maintain it confidently.*
