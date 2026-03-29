# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Foundation phase complete. Structural transformation phase next.*

---

## Goal

Take the inherited xmr-stak CryptoNight-GPU implementation and transform it into a clean, modern, single-purpose miner we can reason about, optimize, and maintain confidently. The algorithm math remains bit-exact — we change organization and expression, not computation.

---

## Principles

1. **Bit-exact output** — Verified via golden test vectors + live mining on 3 GPUs
2. **One module at a time** — Build → test → verify → merge → delete branch → repeat
3. **Test-driven** — Validation harness with known-good hashes before any code change
4. **No magic** — Every function, constant, and parameter has a clear name and purpose
5. **Modern idioms** — `constexpr`, proper naming, RAII, documentation

---

## What We've Done (Session 1: 2026-03-29)

**10 branches merged. 51 files changed. +1,742 / -9,688 = net -7,946 lines (22% smaller)**

### Foundation Phase ✅

| Phase | What | Impact |
|-------|------|--------|
| **R1** | Validation Harness | `tests/cn_gpu_harness.cpp` — 3 golden test vectors verified on 3 machines |
| **R2** | Algorithm Constants | `n0s/algorithm/cn_gpu.hpp` — documented, constexpr, verified |
| **R3** | CPU Crypto Strip | `cryptonight_aesni.h`: 1327→391 lines (-71%) |
| **R4** | CUDA Rename + Docs | 20+ kernel renames, full pipeline documentation |
| **R5** | OpenCL Rename + Docs | Matching renames, fresh compile verified |
| **R6/R7** | Config Simplification | 27 coin_selection call sites → jconf helpers |
| **Dead Code** | File Purge | ASM (10 files), C hashes (8 files), OpenCL hashes (4 files), Windows code, branch kernels/buffers |

### GPU Verification Matrix (every change tested)

| Machine | GPU | Backend | Result |
|---------|-----|---------|--------|
| nitro | AMD RX 9070 XT (RDNA 4) | OpenCL | ✅ Zero rejections |
| nos2 | GTX 1070 Ti (Pascal) | CUDA 11.8 | ✅ Zero rejections |
| nosnode | RTX 2070 (Turing) | CUDA 12.6 | ✅ Zero rejections |

---

## Current Codebase State

```
n0s/
└── algorithm/
    └── cn_gpu.hpp              ← NEW: Clean algorithm constants (202 lines)

xmrstak/                         ← CLEANED but still old structure
├── backend/
│   ├── amd/                     ← OpenCL backend (3,738 lines)
│   │   ├── amd_gpu/
│   │   │   ├── gpu.cpp          ← Host: device init, kernel compile, mining loop
│   │   │   ├── gpu.hpp          ← Host: context struct
│   │   │   └── opencl/
│   │   │       ├── cryptonight.cl      ← Phases 1,2,4,5 kernels (817 lines, was 1164)
│   │   │       ├── cryptonight_gpu.cl  ← Phase 3 FP kernel (RENAMED + DOCUMENTED)
│   │   │       └── wolf-aes.cl         ← AES tables for OpenCL
│   │   ├── autoAdjust.hpp       ← Auto-config (SIMPLIFIED)
│   │   ├── jconf.cpp/hpp        ← AMD config parsing
│   │   └── minethd.cpp/hpp      ← AMD mining thread (SIMPLIFIED)
│   │
│   ├── nvidia/                  ← CUDA backend (4,763 lines)
│   │   ├── nvcc_code/
│   │   │   ├── cuda_cryptonight_gpu.hpp ← Phases 2,3 kernels (RENAMED + DOCUMENTED)
│   │   │   ├── cuda_core.cu            ← Phase 4 kernel + host dispatch (RENAMED + DOCUMENTED)
│   │   │   ├── cuda_extra.cu           ← Phases 1,5 kernels + device init (DOCUMENTED)
│   │   │   ├── cuda_aes.hpp            ← AES for CUDA (needed)
│   │   │   ├── cuda_keccak.hpp         ← Keccak for CUDA (needed)
│   │   │   ├── cuda_blake.hpp          ← ⚠️ DEAD (included but unused)
│   │   │   ├── cuda_groestl.hpp        ← ⚠️ DEAD (included but unused)
│   │   │   ├── cuda_jh.hpp             ← ⚠️ DEAD (included but unused)
│   │   │   ├── cuda_skein.hpp          ← ⚠️ DEAD (included but unused)
│   │   │   ├── cuda_device.hpp         ← Tiny (64 lines)
│   │   │   ├── cuda_compat.hpp         ← Tiny (23 lines)
│   │   │   ├── cuda_extra.hpp          ← Context struct (127 lines)
│   │   │   └── cryptonight.hpp         ← CUDA-side algo defs (64 lines)
│   │   ├── autoAdjust.hpp       ← CUDA auto-config
│   │   ├── jconf.cpp/hpp        ← NVIDIA config parsing
│   │   └── minethd.cpp/hpp      ← NVIDIA mining thread (SIMPLIFIED)
│   │
│   ├── cpu/                     ← CPU hash reference + shared crypto (2,839 lines)
│   │   ├── crypto/
│   │   │   ├── c_keccak.c/h           ← Keccak-1600 (only C file remaining)
│   │   │   ├── cn_gpu_avx.cpp         ← Phase 3 CPU AVX2 impl
│   │   │   ├── cn_gpu_ssse3.cpp       ← Phase 3 CPU SSSE3 impl
│   │   │   ├── cn_gpu.hpp             ← CPU cn_gpu interface
│   │   │   ├── cryptonight_aesni.h    ← CPU hash pipeline (391 lines, was 1327)
│   │   │   ├── cryptonight_common.cpp ← Memory alloc (116 lines, was 320)
│   │   │   ├── cryptonight.h          ← Context struct
│   │   │   └── soft_aes.hpp           ← Software AES fallback
│   │   ├── autoAdjust*.hpp      ← CPU auto-config (dead — CPU mining disabled)
│   │   ├── cpuType.cpp/hpp      ← ⚠️ Dead (ASM variant detection, removed ASM)
│   │   ├── hwlocMemory.cpp/hpp  ← NUMA memory (only used if hwloc enabled)
│   │   ├── jconf.cpp/hpp        ← CPU config
│   │   └── minethd.cpp/hpp      ← CPU mining thread (hash verification only)
│   │
│   ├── cryptonight.hpp    ← Algorithm enum + POW() (shared)
│   ├── globalStates.*     ← Global job queue
│   ├── backendConnector.* ← Backend dispatcher
│   ├── miner_work.hpp     ← Work unit
│   ├── iBackend.hpp       ← Backend interface
│   ├── plugin.hpp         ← dlopen plugin loader
│   └── pool_data.hpp      ← Pool metadata
│
├── net/                   ← Pool connection (1,732 lines)
│   ├── jpsock.cpp/hpp     ← Stratum JSON-RPC (788+146 lines)
│   ├── socket.cpp/hpp     ← TCP/TLS socket (393+63 lines)
│   ├── msgstruct.hpp      ← Message types (243 lines)
│   └── socks.hpp          ← SOCKS proxy (99 lines)
│
├── http/                  ← HTTP monitoring API (492 lines)
├── misc/                  ← Utilities (2,410 lines)
│   ├── executor.cpp/hpp   ← Main coordinator (1,272+192 lines)
│   ├── console.cpp/hpp    ← Console output
│   ├── telemetry.cpp/hpp  ← Hashrate tracking
│   ├── coinDescription.hpp ← ⚠️ DEAD (zero external callers)
│   ├── uac.cpp/hpp        ← ⚠️ Windows UAC (dead on Linux)
│   └── [other utilities]
│
├── cli/cli-miner.cpp     ← Entry point (947 lines)
├── jconf.cpp/hpp          ← Main config (727 lines)
├── params.hpp             ← CLI parameters
├── version.cpp/hpp        ← Version info
├── rapidjson/             ← JSON library (vendored, ~14K lines — don't touch)
└── picosha2/              ← SHA-256 for OpenCL cache (vendored — don't touch)
```

**Codebase: ~35K lines (down from ~43K). Our code: ~21K lines (excluding vendored rapidjson/picosha2)**

---

## Remaining Work — Future Phases

### Phase S1: Final Dead Code Removal (~1,200 lines)

Low-risk, high-reward — remove files that are included but never used.

| Target | Lines | Status |
|--------|-------|--------|
| `cuda_blake.hpp` | 208 | Included in cuda_extra.cu but no functions called |
| `cuda_groestl.hpp` | 326 | Same |
| `cuda_jh.hpp` | 318 | Same |
| `cuda_skein.hpp` | 392 | Same |
| `coinDescription.hpp` | 89 | Zero external callers (jconf uses internally only) |
| `uac.cpp/hpp` | 91 | Windows-only, dead on Linux |
| `cpuType.cpp/hpp` | 108 | ASM variant detection for removed ASM code |
| `read_write_lock.h` | 96 | Zero references |

**Estimated: ~1,628 lines removable. ~2 hours.**

### Phase S2: CUDA File Consolidation

Merge the scattered CUDA files into fewer, logical units:

- `cuda_extra.cu` + `cuda_core.cu` → single `cuda_kernels.cu` (all 5 phases)
- `cuda_device.hpp` + `cuda_compat.hpp` → absorb into `cuda_extra.hpp`
- `cryptonight.hpp` (CUDA-side) → merge with main `cryptonight.hpp`

**Estimated: ~4 hours. Moderate risk (NVCC compilation order matters).**

### Phase S3: OpenCL Cleanup

- `cryptonight.cl` still has multi-algo infrastructure (cn0/cn1/cn2 with ALGO macro). Simplify to direct function names.
- `gpu.cpp` (1,142 lines) is a monolith — split into device_init, kernel_compile, mining_loop
- Remove the `KernelNames` indirection — we know exactly which 4 kernels exist

**Estimated: ~6 hours. Higher risk (OpenCL runtime compilation).**

### Phase S4: Directory Restructuring

Move from `xmrstak/` structure to `n0s/` target layout:
- This is a large rename-only refactor affecting every `#include`
- Should be done as ONE atomic commit to keep git blame useful
- All CMakeLists.txt paths change

**Estimated: ~4 hours. Low risk but high churn. Do last.**

### Phase S5: Namespace Migration

- `xmrstak::` → `n0s::`
- `xmrstak::nvidia::` → `n0s::cuda::`
- Update all references

**Can be done alongside S4 or separately.**

### Phase S6: Modern C++ Patterns (Ongoing)

Apply as opportunities arise, not as a bulk pass:
- Replace raw `new`/`delete` with smart pointers
- Replace C-style casts with `static_cast`/`reinterpret_cast`
- Add `[[nodiscard]]` to functions that return error codes
- Replace `memcpy` with structured copies where safe
- Remove global mutable state where possible

### Phase S7: Pool/Network Documentation

- Document the stratum protocol flow in `jpsock.cpp`
- Document the executor event loop in `executor.cpp`
- Document the job dispatch pipeline
- Add protocol-level comments to `msgstruct.hpp`

**Estimated: ~4 hours. Zero risk (documentation only).**

### Phase S8: Performance Optimization (Future)

Only after all structural work is complete:
- Profile on each GPU architecture
- Optimize shared memory usage in Phase 3 kernel
- Explore occupancy improvements
- Consider CUDA Graphs for kernel chaining

---

## Success Criteria

- [x] All hashes bit-exact with original implementation
- [x] Zero share rejections on all 3 GPU architectures
- [x] Every function documented with its pipeline role
- [x] Single-command build (`cmake .. && make`)
- [ ] Hashrate within 1% of original (needs formal benchmark)
- [ ] No raw `new`/`delete` outside vendored code
- [ ] No global mutable state outside `main()`
- [ ] All `constexpr` where possible
- [ ] Clean compiler output (zero warnings at `-Wall -Wextra`)
- [ ] Directory restructured to `n0s/` layout
- [ ] `xmrstak` namespace fully replaced

---

## Architecture Vision (Updated)

The original vision in this plan was aspirational. After working deeply with the code, here's the **realistic** target that preserves what works while achieving our goals:

```
n0s/
├── algorithm/
│   └── cn_gpu.hpp              ← Constants + types (DONE)
│
├── crypto/
│   ├── keccak.c/h              ← Keccak-1600 (from c_keccak)
│   ├── aes.hpp                 ← AES keygen + rounds (from cryptonight_aesni.h)
│   ├── cn_gpu_cpu.cpp/hpp      ← CPU reference impl (from cn_gpu_avx/ssse3)
│   └── hash_pipeline.hpp       ← Full CPU hash function (from Cryptonight_hash_gpu)
│
├── cuda/
│   ├── kernels.cu              ← All 5 phases (from cuda_core + cuda_extra + cuda_cryptonight_gpu)
│   ├── device.cpp/hpp          ← Device init + memory (from cuda_extra)
│   ├── backend.cpp/hpp         ← Mining thread (from nvidia/minethd)
│   ├── config.cpp/hpp          ← nvidia.txt parsing (from nvidia/jconf)
│   └── auto_tune.hpp           ← Auto-config (from nvidia/autoAdjust)
│
├── opencl/
│   ├── kernels.cl              ← All phases (from cryptonight.cl + cryptonight_gpu.cl)
│   ├── aes.cl                  ← AES tables (from wolf-aes.cl)
│   ├── device.cpp/hpp          ← Device init + kernel compile (from amd/gpu)
│   ├── backend.cpp/hpp         ← Mining thread (from amd/minethd)
│   ├── config.cpp/hpp          ← amd.txt parsing (from amd/jconf)
│   └── auto_tune.hpp           ← Auto-config (from amd/autoAdjust)
│
├── pool/
│   ├── stratum.cpp/hpp         ← JSON-RPC protocol (from net/jpsock)
│   ├── connection.cpp/hpp      ← Socket + TLS (from net/socket)
│   └── job.hpp                 ← Work unit (from miner_work + pool_data)
│
├── core/
│   ├── executor.cpp/hpp        ← Job coordinator (from misc/executor)
│   ├── global_state.hpp        ← Global job queue (from globalStates)
│   ├── backend.hpp             ← Backend interface (from iBackend + backendConnector)
│   └── telemetry.cpp/hpp       ← Hashrate tracking (from misc/telemetry)
│
├── config/
│   ├── config.cpp/hpp          ← Main + pool config (from jconf)
│   ├── params.hpp              ← CLI parameters
│   └── templates/              ← Config templates (.tpl files)
│
├── http/
│   ├── api.cpp/hpp             ← HTTP JSON API
│   └── webdesign.cpp           ← HTML templates
│
├── util/
│   ├── console.cpp/hpp         ← Console output
│   └── environment.hpp         ← Singleton management
│
├── main.cpp                    ← Entry point (from cli/cli-miner.cpp)
│
├── vendor/
│   ├── rapidjson/              ← JSON library (untouched)
│   └── picosha2/               ← SHA-256 for OpenCL cache (untouched)
│
└── tests/
    ├── cn_gpu_harness.cpp      ← Golden test vectors (DONE)
    ├── test_constants.cpp      ← Constants verification (DONE)
    └── build_harness.sh        ← Build script (DONE)
```

### Key Differences from Original Vision:
- **Kept `crypto/` separate from `algorithm/`** — constants vs. implementations
- **Vendor directory** — rapidjson and picosha2 are dependencies, not our code
- **Realistic file mapping** — each target file has a clear source file
- **Tests at top level** — not buried in the tree
- **No abstract GPU interface** — CUDA and OpenCL are too different to share a meaningful base class. Separate implementations with shared algorithm constants is the right pattern.

---

*The code is ours now. The dead weight is gone, the names make sense, and the path forward is clear. We're not rewriting for elegance — we're rewriting for ownership, understanding, and the ability to confidently modify any part of the system.*
