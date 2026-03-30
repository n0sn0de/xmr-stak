# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Foundation + dead code removal complete. CUDA consolidated. Namespace migrated (n0s::). Pool/network documented. Directory restructured (xmrstak/ → n0s/). Zero-warning build. Modern C++ patterns applied. Config/algo simplified. OpenCL constants hardcoded. Windows/macOS/BSD code stripped. Pure C++17 (zero C files). Linux-only.*

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

### Cumulative Total (All Sessions)

The `xmrstak/` directory is **GONE**. All source code now lives under `n0s/`.

**~300 files changed. Net -10,500+ lines removed. Namespace migrated. Directory restructured. Protocol documented. Zero-warning build. Config simplified. Modern C++17. Linux-only. Zero C files.**

---

## Current Codebase State

```
n0s/
├── algorithm/
│   └── cn_gpu.hpp              ← Clean algorithm constants (202 lines)
│
├── backend/
│   ├── amd/                     ← OpenCL backend (~3,650 lines)
│   │   ├── amd_gpu/
│   │   │   ├── gpu.cpp          ← Host: device init, kernel compile, mining loop
│   │   │   ├── gpu.hpp          ← Host: context struct
│   │   │   └── opencl/
│   │   │       ├── cryptonight.cl      ← Phase 4+5 kernel + shared helpers
│   │   │       ├── cryptonight_gpu.cl  ← Phase 1,2,3 kernels (cn_gpu_phase*)
│   │   │       └── wolf-aes.cl         ← AES tables for OpenCL
│   │   ├── autoAdjust.hpp       ← Auto-config
│   │   ├── jconf.cpp/hpp        ← AMD config parsing
│   │   └── minethd.cpp/hpp      ← AMD mining thread
│   │
│   ├── nvidia/                  ← CUDA backend (~4,500 lines, CONSOLIDATED)
│   │   ├── nvcc_code/
│   │   │   ├── cuda_cryptonight_gpu.hpp ← Phases 2,3 kernels
│   │   │   ├── cuda_kernels.cu         ← Phases 1,4,5 + host dispatch + device mgmt
│   │   │   ├── cuda_aes.hpp            ← AES for CUDA
│   │   │   ├── cuda_keccak.hpp         ← Keccak for CUDA
│   │   │   ├── cuda_extra.hpp          ← Utility macros + compat shims + error checking
│   │   │   └── cuda_context.hpp        ← nvid_ctx struct + extern "C" ABI
│   │   ├── autoAdjust.hpp       ← CUDA auto-config
│   │   ├── jconf.cpp/hpp        ← NVIDIA config parsing
│   │   └── minethd.cpp/hpp      ← NVIDIA mining thread
│   │
│   ├── cpu/                     ← CPU hash reference + shared crypto (2,839 lines)
│   │   ├── crypto/
│   │   │   ├── keccak.cpp/hpp         ← Keccak-1600 (converted to C++ in Session 8)
│   │   │   ├── cn_gpu_avx.cpp         ← Phase 3 CPU AVX2 impl
│   │   │   ├── cn_gpu_ssse3.cpp       ← Phase 3 CPU SSSE3 impl
│   │   │   ├── cn_gpu.hpp             ← CPU cn_gpu interface
│   │   │   ├── cryptonight_aesni.h    ← CPU hash pipeline (391 lines)
│   │   │   ├── cryptonight_common.cpp ← Memory alloc (116 lines)
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
│   ├── jpsock.cpp/hpp     ← Stratum JSON-RPC
│   ├── socket.cpp/hpp     ← TCP/TLS socket
│   ├── msgstruct.hpp      ← Message types
│   └── socks.hpp          ← SOCKS proxy
│
├── http/                  ← HTTP monitoring API (492 lines)
├── misc/                  ← Utilities (2,410 lines)
│   ├── executor.cpp/hpp   ← Main coordinator
│   ├── console.cpp/hpp    ← Console output
│   ├── telemetry.cpp/hpp  ← Hashrate tracking
│   ├── (coinDescription.hpp removed — Session 7)
│   └── [other utilities]
│
├── cli/cli-miner.cpp     ← Entry point (947 lines)
├── jconf.cpp/hpp          ← Main config (727 lines)
├── params.hpp             ← CLI parameters
├── version.cpp/hpp        ← Version info
│
├── vendor/
│   ├── rapidjson/         ← JSON library (vendored, ~14K lines — don't touch)
│   └── picosha2/          ← SHA-256 for OpenCL cache (vendored — don't touch)
│
└── (cpputil/ removed — replaced with std::shared_mutex)

tests/
├── cn_gpu_harness.cpp     ← Golden test vectors
├── test_constants.cpp     ← Constants verification
└── build_harness.sh       ← Build script
```

**Codebase: ~30K lines (down from ~43K). Our code: ~16.1K lines (excluding vendored rapidjson/picosha2)**
**The `xmrstak/` directory is GONE. Everything is `n0s/` now. Zero C files remain.**

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
| `read_write_lock.h` | 96 | ✅ Removed — replaced with std::shared_mutex (Session 6) |

### Phase S2: CUDA File Consolidation ✅ COMPLETE

| Target | Status |
|--------|--------|
| `cuda_extra.cu` + `cuda_core.cu` → `cuda_kernels.cu` | ✅ Merged |
| `cuda_device.hpp` + `cuda_compat.hpp` → `cuda_extra.hpp` | ✅ Absorbed |
| `cryptonight.hpp` (CUDA-side) → `cuda_context.hpp` | ✅ Renamed (not merged with main — different purpose) |

CUDA files reduced from 9 → 5. Live-tested on nos2 (GTX 1070 Ti) and nosnode (RTX 2070).

### Phase S3: OpenCL Cleanup — MOSTLY COMPLETE

| Target | Status |
|--------|--------|
| Rename kernels to explicit names (remove JOIN/ALGO macro) | ✅ Done (cn_gpu_phase*) |
| Remove KernelNames vector indirection | ✅ Done (static const char*) |
| Strip dead Windows code from gpu.cpp | ✅ Done (-46 lines) |
| Remove dead JOIN_DO/JOIN macros | ✅ Done |
| Remove STRIDED_INDEX dead branches | ✅ Done — hardcoded to 0 (Session 7, S3.2) |
| Remove MEM_CHUNK/mem_chunk/strided_index config | ✅ Done — accepted but ignored (Session 7) |
| Delete dead cryptonight_r*.rtcl files | ✅ Done (Session 7) |
| `gpu.cpp` split (device_init, kernel_compile, mining_loop) | ❌ Future — ~990 lines, still monolithic |
| COMP_MODE conditionals | ✅ Kept — live feature for non-aligned intensity |

**Remaining: gpu.cpp split (~4 hours). Deferred — risk/reward not favorable yet.**

### Phase S4: Directory Restructuring ✅ COMPLETE

Moved from `xmrstak/` structure to `n0s/` layout:
- 133 files renamed via `git mv` (git tracks full rename history)
- All 161 `#include "xmrstak/..."` → `#include "n0s/..."`
- Vendored libraries moved to `n0s/vendor/` (rapidjson, picosha2)
- CMakeLists.txt glob paths, scripts, and tests all updated
- ONE atomic commit — git blame preserved

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

### Phase S6: Modern C++ Patterns — MAJOR PROGRESS (Session 6)

**S6.1: Zero-Warning Build ✅ COMPLETE**
Fixed ~80 compiler warnings across 18 files to achieve clean `-Wall -Wextra`:
- Member initialization order fixes (params, globalStates, coinDescription, msgstruct)
- Sign-compare: int vs size_t loop variables throughout codebase
- Unused variables removed (gpu.cpp: isHSAOpenCL, tmpNonce, platforms; cn_gpu_avx: d01/d23)
- Unused parameters: `[[maybe_unused]]` on callback/API params
- memset on non-trivial `pool_job` → value initialization
- **Bug fix**: miner_work move ctor was memcpy'ing sJobID from self (!)
- **Bug fix**: miner_work move ctor was not initializing bNiceHash
- Deprecated OpenSSL: replace legacy init with `OPENSSL_init_ssl()`
- addrinfo: zero-init with `{}` instead of `{0}`

**S6.2: std::shared_mutex Migration ✅ COMPLETE**
- Replaced custom `cpputil::RWLock` (96 lines) with C++17 `std::shared_mutex`
- RAII lock guards (`std::shared_lock` / `std::unique_lock`) eliminate manual unlock
- Deleted `n0s/cpputil/` directory entirely (-121 lines)

**S6.3: Safe Move Semantics ✅ COMPLETE**
- `pool_job`: default member initializers (zero sJobID, bWorkBlob, iTarget)
- `miner_work`, `sock_err`, `ex_event`: replaced `assert(this!=&from)` with if-guard
  (self-move-assign is now safe per the standard)

**Remaining (apply as opportunities arise):**
- Replace raw `new`/`delete` with smart pointers
- Replace C-style casts with `static_cast`/`reinterpret_cast`
- Add `[[nodiscard]]` to functions that return error codes
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

### Phase S8: Config & Algorithm Simplification ✅ COMPLETE (Session 7)

**S8.1: n0s_algo Struct Simplification ✅**
- Collapsed 5 constructors to 2 (default + full parameterized)
- Removed `algo_name`/`base_algo` duality — single `id` member (always identical for cn_gpu)
- Cleaner member names: `id`, `iter`, `mem`, `mask`
- All pool protocol fields preserved (Name/BaseName/Iter/Mem/Mask)

**S8.2: Delete coinDescription.hpp ✅ (-89 lines)**
- Removed `coinDescription` struct (algo + algo_root + fork_version — root and fork_version never used)
- Removed `coin_selection` struct (coin_name + 2 coinDescriptions + default_pool)
- Removed coin lookup loops in jconf.cpp — replaced with direct `isValidCurrency()` check
- Removed `currentCoin` member from jconf class

**S8.3: Dead Code Removal ✅**
- Removed `cached_algo` from `nvid_ctx` (CUDA context — declared but never read)
- Removed `last_algo` from `cryptonight_ctx` (CPU context — only written, never read)

### Phase S3.2: OpenCL Constant Cleanup ✅ COMPLETE (Session 7)

- Hardcoded `STRIDED_INDEX=0` in OpenCL kernels (cn_gpu always uses direct indexing)
- Removed `STRIDED_INDEX` 1/2/3 branches from `cryptonight.cl` and `cryptonight_gpu.cl`
- Removed `MEM_CHUNK` macro (only used by dead `STRIDED_INDEX=2`)
- Removed `-DSTRIDED_INDEX=` and `-DMEM_CHUNK_EXPONENT=` from OpenCL compiler options
- Removed `stridedIndex`/`memChunk` from `GpuContext`, jconf `thd_cfg`, and config parsing
  (strided_index/mem_chunk still accepted in config files for backward compat, just ignored)
- Removed dead stridedIndex 2/3 intensity adjustment check in gpu.cpp
- Deleted dead `cryptonight_r*.rtcl` files (CryptoNight-R variant, never used by cn_gpu)
- `COMP_MODE` kept — it's a live runtime feature for non-aligned intensity

### Phase S9: Dead Code Sweep ✅ COMPLETE (Session 7)

- Removed dead `CNKeccak()` function from `cryptonight.cl` (defined but never called)
- Removed stale "Dead code removed" tombstone comments and commented-out includes
- Removed dead `if(false)` block in `gpu.cpp` (MaximumWorkSize /= 8)
- Cleaned up keccak function comments to reflect actual usage

### Phase S10: Strip Windows/macOS/BSD Platform Code ✅ COMPLETE (Session 8)

**S10.0: Strip Platform Code ✅ (-466 lines)**
- Removed all `#ifdef _WIN32`, `__APPLE__`, `__FreeBSD__`, `__OpenBSD__`, `_MSC_VER` blocks
- Linux-only: POSIX sockets, termios, dlopen, pthread — no Windows alternatives
- Renamed `win_exit()` → `n0s_exit()` across entire codebase
- Simplified 26 files: console, socks, plugin, home_dir, configEditor, jext, etc.

**S10.1: Remove int_port() MSVC Workaround ✅ (-6 lines)**
- `int_port()` was a `sizeof(long)` workaround for MSVC `%llu` format strings
- Replaced all `%llu` → `%zu` for `size_t` format specifiers
- Updated JSON API format strings in webdesign.cpp
- Fixed `duration_cast` parenthesization in executor.cpp

**S10.2: CMakeLists.txt Linux-Only ✅ (-13 lines)**
- Removed MSVC compiler guards, Windows socket libs, WIN32 pthread guards

**S10.3: Dead Params + CLI Cleanup ✅ (-15 lines)**
- Removed `allowUAC` (Windows UAC — dead), `configFileCPU` (CPU mining disabled)
- Removed `--noUAC` CLI handler, `--noCPU`/`--cpu` help text
- Removed `N0S_DEV_RELEASE` guard from CUDA library loading

### Phase S11: Convert c_keccak.c → keccak.cpp ✅ COMPLETE (Session 8)

- Renamed c_keccak.c/h → keccak.cpp/hpp (last C file eliminated)
- Modernized: `constexpr` for constants, `reinterpret_cast`, C++ headers
- Simplified Chi step with loop instead of manually unrolled blocks
- Added `extern "C"` linkage for compatibility with cryptonight_aesni.h
- Eliminated `n0s-c` static library build target (was only for c_keccak.c)
- Build targets: 4 → 3 (n0s-backend, n0s-ryo-miner, n0s_{cuda,opencl}_backend)
- **Zero C files remain — pure C++17 codebase**

### Phase S12: Modernize C Headers ✅ COMPLETE (Session 8)

- Replaced all C-style `#include` with C++ equivalents across 34 files:
  `assert.h` → `cassert`, `stdlib.h` → `cstdlib`, `string.h` → `cstring`,
  `stdio.h` → `cstdio`, `stdint.h` → `cstdint`, etc.
- Zero functional change, pure idiom modernization

### Phase P1: Performance Optimization (Future)

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
- [x] Clean compiler output (zero warnings at `-Wall -Wextra`) — Session 6
- [x] Directory restructured to `n0s/` layout (S4, Session 5)
- [x] `xmrstak` namespace fully replaced → `n0s::` (S5, Session 4)
- [x] Config/algo system simplified — single-algorithm focus (S8, Session 7)
- [x] OpenCL dead kernel branches removed (S3.2, Session 7)
- [x] Linux-only — all Windows/macOS/BSD platform code removed (S10, Session 8)
- [x] Pure C++17 — zero C files remain (S11, Session 8)
- [x] Modern C++ headers everywhere (S12, Session 8)

---

## Architecture Vision (Updated)

The original vision in this plan was aspirational. After working deeply with the code, here's the **realistic** target that preserves what works while achieving our goals:

```
n0s/
├── algorithm/
│   └── cn_gpu.hpp              ← Constants + types (DONE)
│
├── crypto/
│   ├── keccak.cpp/hpp           ← Keccak-1600 (DONE — converted from c_keccak.c)
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
