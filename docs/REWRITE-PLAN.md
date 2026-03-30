# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Foundation + dead code removal complete. CUDA consolidated. Namespace migrated (n0s::). Pool/network documented.*

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

### Session 2 (2026-03-29, continued)

**3 branches merged. +46 / -1,888 lines.**

| Phase | What | Impact |
|-------|------|--------|
| **S1** | CUDA dead hash removal | Removed cuda_blake/groestl/jh/skein.hpp (-1,244 lines) |
| **S1** | ASM variant removal | Removed cpuType, asm_version_str plumbing, UAC (-259 lines) |
| **S1** | OpenCL context simplification | Program map→single, Kernels map→array[4], ExtraBuffers[6]→[2] |
| **S1** | OpenCL dead kernel strip | cryptonight.cl: 817→521 (-36%): removed cn0, cn1, dead helpers |

### Session 3 (2026-03-29, continued)

**5 branches merged. +455 / -542 = net -87 lines. Structural consolidation.**

| Phase | What | Impact |
|-------|------|--------|
| **S2.1** | CUDA header consolidation | Merged cuda_device.hpp + cuda_compat.hpp → cuda_extra.hpp (-2 files) |
| **S2.2** | CUDA context rename | cryptonight.hpp (CUDA) → cuda_context.hpp (eliminates name confusion) |
| **S2.3** | CUDA kernel consolidation | cuda_extra.cu + cuda_core.cu → cuda_kernels.cu (single file, all 5 phases) |
| **S3.1** | OpenCL kernel rename | Removed JOIN(name,ALGO) macro — kernels have explicit names (cn_gpu_phase*) |
| **S3.2** | OpenCL dead code strip | Removed Windows code, JOIN macro, MSVC fallbacks from gpu.cpp/cl (-46 lines) |

Live mining tested on all 3 GPUs after every change — zero rejections.

### Session 4 (2026-03-29, continued)

**3 branches merged. 66 files changed. +843 / -278 = net +565 lines (documentation added).**

| Phase | What | Impact |
|-------|------|--------|
| **S5** | Namespace migration | `xmrstak::` → `n0s::`, `nvidia::` → `cuda::`, `amd::` → `opencl::` |
| **S5** | Type renames | `xmrstak_algo` / `xmrstak_algo_id` → `n0s_algo` / `n0s_algo_id` |
| **S5** | Macro renames | `XMR_STAK_*` → `N0S_*`, `XMRSTAK_*` → `N0S_*` (all macros) |
| **S5** | ABI symbol renames | `xmrstak_start_backend` → `n0s_start_backend`, lib targets updated |
| **S5** | CMake target renames | `xmr-stak-c/backend` → `n0s-c/backend`, plugin libs renamed |
| **S5.1** | Remaining XMRSTAK refs | Templates (.tpl), env vars, dev macros — all → N0S_ |
| **S7** | Pool/network docs | `docs/POOL-NETWORK.md`: comprehensive stratum protocol documentation |
| **S7** | Module-level docs | executor.cpp, jpsock.cpp, socket.cpp, msgstruct.hpp documented |

Live mining tested on all 3 GPUs — zero rejections.
Include paths (`#include "xmrstak/..."`) preserved — those change with directory restructuring (S4).

### Cumulative Total (All Sessions)

**~72 files changed. +3,319 / -12,578 = net -9,259 lines. Namespace fully migrated. Protocol documented.**

---

## Current Codebase State

```
n0s/
└── algorithm/
    └── cn_gpu.hpp              ← NEW: Clean algorithm constants (202 lines)

xmrstak/                         ← CLEANED, CUDA consolidated, OpenCL partially cleaned
├── backend/
│   ├── amd/                     ← OpenCL backend (~3,650 lines)
│   │   ├── amd_gpu/
│   │   │   ├── gpu.cpp          ← Host: device init, kernel compile, mining loop (CLEANED: Windows code removed)
│   │   │   ├── gpu.hpp          ← Host: context struct
│   │   │   └── opencl/
│   │   │       ├── cryptonight.cl      ← Phase 4+5 kernel + shared helpers (CLEANED: JOIN macro removed)
│   │   │       ├── cryptonight_gpu.cl  ← Phase 1,2,3 kernels (RENAMED: cn_gpu_phase* names)
│   │   │       └── wolf-aes.cl         ← AES tables for OpenCL
│   │   ├── autoAdjust.hpp       ← Auto-config (SIMPLIFIED)
│   │   ├── jconf.cpp/hpp        ← AMD config parsing
│   │   └── minethd.cpp/hpp      ← AMD mining thread (SIMPLIFIED)
│   │
│   ├── nvidia/                  ← CUDA backend (~4,500 lines, CONSOLIDATED)
│   │   ├── nvcc_code/
│   │   │   ├── cuda_cryptonight_gpu.hpp ← Phases 2,3 kernels (RENAMED + DOCUMENTED)
│   │   │   ├── cuda_kernels.cu         ← Phases 1,4,5 + host dispatch + device mgmt (CONSOLIDATED from cuda_core.cu + cuda_extra.cu)
│   │   │   ├── cuda_aes.hpp            ← AES for CUDA (needed)
│   │   │   ├── cuda_keccak.hpp         ← Keccak for CUDA (needed)
│   │   │   ├── cuda_extra.hpp          ← Utility macros + compat shims + error checking (CONSOLIDATED from cuda_device.hpp + cuda_compat.hpp)
│   │   │   └── cuda_context.hpp        ← nvid_ctx struct + extern "C" ABI (RENAMED from cryptonight.hpp)
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
│   ├── coinDescription.hpp ← Internal to jconf (keep for now)
│   └── [other utilities]
│
├── cli/cli-miner.cpp     ← Entry point (947 lines)
├── jconf.cpp/hpp          ← Main config (727 lines)
├── params.hpp             ← CLI parameters
├── version.cpp/hpp        ← Version info
├── rapidjson/             ← JSON library (vendored, ~14K lines — don't touch)
└── picosha2/              ← SHA-256 for OpenCL cache (vendored — don't touch)
```

**Codebase: ~31K lines (down from ~43K). Our code: ~17K lines (excluding vendored rapidjson/picosha2)**

---

## Remaining Work — Future Phases

### Phase S1: Final Dead Code Removal ✅ COMPLETE

| Target | Lines | Status |
|--------|-------|--------|
| `cuda_blake.hpp` | 208 | ✅ Removed |
| `cuda_groestl.hpp` | 326 | ✅ Removed |
| `cuda_jh.hpp` | 318 | ✅ Removed |
| `cuda_skein.hpp` | 392 | ✅ Removed |
| `uac.cpp/hpp` | 91 | ✅ Removed |
| `cpuType.cpp/hpp` | 108 | ✅ Removed (CPUID check inlined in autoAdjust) |
| `asm_version_str` | ~50 | ✅ Removed from entire call chain |
| OpenCL cn0/cn1 kernels | 296 | ✅ Stripped from cryptonight.cl |
| GpuContext maps | ~30 | ✅ Simplified to direct types |
| `coinDescription.hpp` | 89 | Deferred — used internally by jconf coin lookup |
| `read_write_lock.h` | 96 | Kept — used by globalStates::jobLock (replace with std::shared_mutex later) |

### Phase S2: CUDA File Consolidation ✅ COMPLETE

| Target | Status |
|--------|--------|
| `cuda_extra.cu` + `cuda_core.cu` → `cuda_kernels.cu` | ✅ Merged |
| `cuda_device.hpp` + `cuda_compat.hpp` → `cuda_extra.hpp` | ✅ Absorbed |
| `cryptonight.hpp` (CUDA-side) → `cuda_context.hpp` | ✅ Renamed (not merged with main — different purpose) |

CUDA files reduced from 9 → 5. Live-tested on nos2 (GTX 1070 Ti) and nosnode (RTX 2070).

### Phase S3: OpenCL Cleanup — IN PROGRESS

| Target | Status |
|--------|--------|
| Rename kernels to explicit names (remove JOIN/ALGO macro) | ✅ Done (cn_gpu_phase*) |
| Remove KernelNames vector indirection | ✅ Done (static const char*) |
| Strip dead Windows code from gpu.cpp | ✅ Done (-46 lines) |
| Remove dead JOIN_DO/JOIN macros | ✅ Done |
| `gpu.cpp` split (device_init, kernel_compile, mining_loop) | ❌ Future — 1,015 lines, still monolithic |
| Remove remaining multi-algo conditionals in .cl files | ❌ Future — COMP_MODE, STRIDED_INDEX still present |

**Remaining: gpu.cpp split (~4 hours). Deferred — risk/reward not favorable yet.**

### Phase S4: Directory Restructuring

Move from `xmrstak/` structure to `n0s/` target layout:
- This is a large rename-only refactor affecting every `#include`
- Should be done as ONE atomic commit to keep git blame useful
- All CMakeLists.txt paths change

**Estimated: ~4 hours. Low risk but high churn. Next major phase.**

### Phase S5: Namespace Migration ✅ COMPLETE

All namespace/type/macro references migrated:
- `namespace xmrstak` → `namespace n0s`
- `namespace nvidia` → `namespace cuda`
- `namespace amd` → `namespace opencl`
- `xmrstak_algo` / `xmrstak_algo_id` → `n0s_algo` / `n0s_algo_id`
- All `XMR_STAK_*` / `XMRSTAK_*` macros → `N0S_*`
- ABI symbols: `xmrstak_start_backend` → `n0s_start_backend`
- Config templates updated
- CMake targets renamed (`n0s-c`, `n0s-backend`, `n0s_cuda_backend`, `n0s_opencl_backend`)

Only `#include "xmrstak/..."` paths remain — intentionally deferred to S4 (directory restructuring).

### Phase S6: Modern C++ Patterns (Ongoing)

Apply as opportunities arise, not as a bulk pass:
- Replace raw `new`/`delete` with smart pointers
- Replace C-style casts with `static_cast`/`reinterpret_cast`
- Add `[[nodiscard]]` to functions that return error codes
- Replace `memcpy` with structured copies where safe
- Remove global mutable state where possible

### Phase S7: Pool/Network Documentation ✅ COMPLETE

- `docs/POOL-NETWORK.md`: Comprehensive protocol documentation including:
  - Architecture overview (executor → jpsock → socket → pool)
  - Full stratum message format (login, job, submit) with JSON examples
  - Threading model and memory management
  - Event system (discriminated union, event types, event loop)
  - Job dispatch pipeline (pool → globalStates → GPU threads)
  - Share submission pipeline (GPU → executor → pool, with CPU verification)
  - Multi-pool failover algorithm and pool lifecycle
  - Configuration reference and error handling summary
- Module-level doc comments added to executor.cpp, jpsock.cpp, socket.cpp, msgstruct.hpp

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
- [x] `xmrstak` namespace fully replaced → `n0s::` (S5, Session 4)

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
│   ├── kernels.cu              ← All 5 phases (PARTIALLY DONE: cuda_kernels.cu has phases 1,4,5 + host; cuda_cryptonight_gpu.hpp has 2,3)
│   ├── device.cpp/hpp          ← Device init + memory (future: extract from cuda_kernels.cu)
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
