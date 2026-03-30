# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Foundation + dead code removal complete. CUDA consolidated. Namespace migrated (n0s::). Pool/network documented. Directory restructured (xmrstak/ → n0s/). Zero-warning build. Modern C++ patterns applied. Config/algo simplified. OpenCL constants hardcoded. Windows/macOS/BSD code stripped. Pure C++17 (zero C files). Linux-only. Smart pointers replacing raw new/delete. std::regex eliminated from hot paths.*

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

## Cumulative Progress (All Sessions)

**Session 20 (2026-03-30 05:05 AM):**
- ✅ Expanded constexpr to compile-time computable functions
- ✅ Made n0s_algo constructors constexpr (default, single-arg, full)
- ✅ Made POW() constexpr (algorithm lookup)
- ✅ Made jconf getters constexpr: GetMiningAlgo(), GetMiningMemSize(), HaveHardwareAes()
- ✅ Made msgstruct converters constexpr: t32_to_t64, t64_to_diff, diff_to_t64
- ✅ Made get_masked() constexpr (CPU autoAdjust bit extraction)
- ✅ Added const to jpsock simple getters (get_pool_addr, get_tls_fp, get_rigid, is_nicehash)
- Net: 5 files changed, 19 insertions(+), 19 deletions(-) — zero behavior changes, bit-exact hashes verified

## Cumulative Progress (All Sessions)

**Session 20 (2026-03-30 05:05 AM):**
- ✅ Expanded constexpr to compile-time computable functions
- ✅ Made n0s_algo constructors constexpr (default, single-arg, full)
- ✅ Made POW() constexpr (algorithm lookup)
- ✅ Made jconf getters constexpr: GetMiningAlgo(), GetMiningMemSize(), HaveHardwareAes()
- ✅ Made msgstruct converters constexpr: t32_to_t64, t64_to_diff, diff_to_t64
- ✅ Made get_masked() constexpr (CPU autoAdjust bit extraction)
- ✅ Added const to jpsock simple getters (get_pool_addr, get_tls_fp, get_rigid, is_nicehash)
- Net: 5 files changed, 19 insertions(+), 19 deletions(-) — zero behavior changes, bit-exact hashes verified

**Session 18 (2026-03-30 03:35 AM):**
- ✅ Split AMD `gpu.cpp` (1003 lines) into focused modules: gpu_utils, gpu_platform, gpu_device, gpu (4 files, 373-421 lines each)
- ✅ Wrapped all AMD GPU functions in `n0s::amd` namespace (was global scope)
- ✅ Updated minethd.cpp and autoAdjust.hpp with cross-namespace `using` declarations
- Net: +220 lines (8 new files, better organization), zero behavior changes
- AMD backend now cleanly organized: utilities, platform discovery, device init, main interface

**Session 17 (2026-03-30 02:50 AM):**
- ✅ Added `[[nodiscard]]` to 40+ critical error-returning functions (network, backends, executor, httpd)
- ✅ Properly handled all call sites (error checks in AMD minethd, (void) casts for error logging)
- ✅ Added `constexpr` to compile-time computable functions (iBackend::getName, executor::sec_to_ticks)
- Improves code safety by making it a compiler error to ignore critical error returns
- Zero new warnings, zero behavior changes, bit-exact hashes verified

**Session 16 (2026-03-30):**
- ✅ Eliminated pointer-to-vector pattern in thread_starter (AMD, NVIDIA, CPU, BackendConnector)
- ✅ Fixed socket memory leak in jpsock (sck → unique_ptr<base_socket>)
- ✅ Modernized all PIMPL patterns (5x opaque_private → unique_ptr)
- Net: -10 raw new/delete pairs, 6 memory leaks fixed
- Remaining raw `new`: 13 (8 singletons, 3 minethd thread objects, 2 backend singletons)

~335 files changed. Net -10,300+ lines removed. Our code: ~16,350 lines (down from ~43K). Clean C++17, zero warnings, zero C files, Linux-only, single-purpose. Smart pointers + RAII replacing manual memory management. [[nodiscard]] on critical functions. AMD + NVIDIA backends modular. constexpr on compile-time functions.

---

## Remaining Work

### Near-Term Opportunities

**Next Session Targets:**
1. **Documentation pass** (~2 hours) — Add function-level comments to complex GPU kernels (Phase 2, Phase 3, etc.)
2. **More constexpr expansion** (~1-2 hours) — Look for more lookup tables, accessor functions
3. **Fix CUDA deprecation warnings** (~1 hour) — Replace deprecated intrinsics in cuda_cryptonight_gpu.hpp

**Completed Modernizations:**
- ✅ **AMD GPU modularization** — Monolithic 1003-line gpu.cpp split into 4 focused modules (S18)
- ✅ **NVIDIA backend modularization** — Monolithic 832-line cuda_kernels.cu split into 4 focused modules (S19)
- ✅ **[[nodiscard]]** — 40+ critical error-returning functions (S17)
- ✅ **constexpr** — Core algorithm functions (S17: getName, sec_to_ticks). Algorithm constants (S15). Expanded to constructors, getters, converters (S20)
- ✅ **Smart pointers** — Thread vectors, socket, PIMPL (S16). Telemetry, jpsock buffers/thread, executor telem (S9). 13 raw `new` remain (singletons + minethd — intentional)
- ✅ **Modern casts** — Host code done (S9). Only CUDA device code + soft_aes macro retain C-style casts
- ✅ **NULL → nullptr** — Host code done (S9, 68 replacements)
- ✅ **std::regex removal** — gpu.cpp done (S9). configEditor.hpp still uses regex (genuine pattern matching — keep it)
- ✅ **Unused includes** — 13 removed (S9). Remaining verified needed

### Performance Optimization (P1)
Only after structural work is complete (check the Remaining things in succes criteria):
- Profile on each GPU architecture (AMD RDNA4, NVIDIA Pascal/Turing/Ampere)
- Optimize shared memory usage in Phase 3 kernel
- Explore occupancy improvements
- Consider CUDA Graphs for kernel chaining

---

## Success Criteria

**Completed:**
- ✅ Bit-exact hashes + zero share rejections on 3 GPU architectures
- ✅ Zero-warning build (`-Wall -Wextra`)
- ✅ Single-command build (`cmake .. && make`)
- ✅ Pure C++17 (zero C files), Linux-only
- ✅ Directory restructured to `n0s/` layout
- ✅ `xmrstak` namespace fully replaced → `n0s::`
- ✅ Config/algo system simplified (single-algorithm focus)
- ✅ OpenCL dead kernel branches removed
- ✅ Modern C++ headers everywhere (`<cstdint>` not `<stdint.h>`)

**Remaining:**
- ⏳ No raw `new`/`delete` outside vendored code
- ⏳ No global mutable state outside `main()`
- ⏳ All `constexpr` where possible
- ⏳ Create hashrate benchmark testing harness that always stops mining at the end before beginning performance optimizations
- ⏳ Algorithm/Kernel Autotuning based on users hardware (see /docs/PRD_01-AUTOTUNING.md)

---

---

### Key Differences from Initial Vision:
- **Kept `crypto/` separate from `algorithm/`** — constants vs. implementations
- **Vendor directory** — rapidjson and picosha2 are dependencies, not our code
- **Realistic file mapping** — each target file has a clear source file
- **Tests at top level** — not buried in the tree
- **No abstract GPU interface** — CUDA and OpenCL are too different to share a meaningful base class. Separate implementations with shared algorithm constants is the right pattern.

---

## Session 17 Notes (2026-03-30 02:50 AM)

**What we accomplished:**
- Added `[[nodiscard]]` to 40+ critical functions — compiler now enforces checking error returns
- Properly handled all AMD GPU error paths (XMRSetJob/XMRRunJob now checked with logging/retry)
- Socket error logging uses explicit (void) casts to document intentional ignore
- Added constexpr to compile-time functions (iBackend::getName, executor::sec_to_ticks)
- Zero warnings, bit-exact hashes verified, all tests pass

**Key insights:**
- `[[nodiscard]]` is powerful for safety — compiler catches silent failures at compile time
- Some error-logging functions return false by design (return set_error(...) pattern)
- For standalone error logs in already-failed paths, (void) cast documents intentional ignore
- constexpr opportunities exist but need careful analysis (macros → constexpr needs full namespace support)

---

## Session 18 Notes (2026-03-30 03:35 AM)

**What we accomplished:**
- Split monolithic AMD `gpu.cpp` (1003 lines) into focused modules:
  - `gpu_utils.cpp/hpp` (78 lines): Helper functions (LoadTextFile, string_replace_all, etc.)
  - `gpu_platform.cpp/hpp` (212 lines): Platform/device discovery (getNumPlatforms, getAMDPlatformIdx, getAMDDevices)
  - `gpu_device.cpp/hpp` (421 lines): Device initialization + timing (InitOpenCLGpu, updateTimings, interleaveAdjustDelay)
  - `gpu.cpp` (373 lines): Main OpenCL interface (InitOpenCL, XMRSetJob, XMRRunJob)
- Wrapped all functions in `n0s::amd` namespace (was global scope)
- Updated `minethd.cpp` and `autoAdjust.hpp` with `using` declarations for cross-namespace access
- Zero warnings, bit-exact hashes verified via test harness

**Key insights:**
- Single-responsibility modules are far easier to reason about than 1000-line monoliths
- Namespace wrapping forces explicit visibility — no more accidental global pollution
- CMake glob patterns (`*.cpp`) automatically pick up new files — no manual listing needed
- Cross-namespace `using` declarations keep client code clean without polluting every file

---

## Session 19 Notes (2026-03-30 04:20 AM)

**What we accomplished:**
- Split monolithic NVIDIA `cuda_kernels.cu` (832 lines) into 4 focused modules:
  - `cuda_device.cu/hpp` (322 lines): Device management (init, enumeration, capability checking)
  - `cuda_phase1.cu/hpp` (127 lines): Phase 1 kernel (Keccak + AES key expansion)
  - `cuda_phase4_5.cu/hpp` (248 lines): Phase 4 + 5 kernels (implode + finalize)
  - `cuda_dispatch.cu/hpp` (180 lines): Host-side kernel dispatch
- `cuda_kernels.cu` now a 21-line compatibility shim that includes the new headers
- Total: 877 lines across 4 files (net +45 lines for headers), zero behavior changes
- OpenCL build verified (zero warnings), CUDA compilation warnings only (deprecated CUDA intrinsics in existing code)
- Fixed container-build.sh: added `HWLOC_ENABLE=OFF` flag for containerized builds

**Key insights:**
- Mirrored Session 18's AMD refactor pattern: single-responsibility modules, namespace-clean
- Explicit template instantiations needed at bottom of .cu files for NVCC separate compilation
- CUDA deprecation warnings exist in `cuda_cryptonight_gpu.hpp` (int2float → __int2float_rn, etc.) — future cleanup target
- Container builds need explicit feature disables when deps aren't needed (hwloc for generic builds)

**Next session priorities:**
1. **More constexpr** (~2 hours) — Expand to remaining compile-time constants (lookup tables, simple getters, config values)
2. **Documentation pass** (~2 hours) — Add function-level comments to complex GPU kernels (Phase 2, Phase 3, etc.)
3. **Fix CUDA deprecation warnings** (~1 hour) — Replace deprecated intrinsics in cuda_cryptonight_gpu.hpp

**Lessons learned:**
- Refactoring patterns transfer cleanly between backends (AMD OpenCL → NVIDIA CUDA)
- Modular code compiles faster (parallel nvcc invocations on separate .cu files)
- Container builds expose missing flags that local builds hide (hwloc present on host but not in container)

---

## Session 20 Notes (2026-03-30 05:05 AM)

**What we accomplished:**
- Expanded constexpr to compile-time computable functions across 5 files
- Made n0s_algo constructors constexpr → enables POW() to be constexpr
- Made algorithm lookup (POW), config getters (GetMiningAlgo, GetMiningMemSize), and difficulty converters constexpr
- Added const correctness to jpsock simple getters (pool_addr, tls_fp, rigid, nicehash)
- Avoided constexpr on atomic member accessors (compiler would reject)
- Zero warnings, bit-exact hashes verified via test harness

**Key insights:**
- constexpr enables compile-time evaluation of pure functions — more optimization, clearer intent
- Can't mark functions constexpr if they access atomics or call non-constexpr members
- Constructors can be constexpr if they only initialize member variables with compile-time expressions
- constexpr implies inline, so inline keyword becomes redundant
- Test harness critical for verifying no behavior changes (3/3 golden hashes pass)

**Next session priorities:**
1. **Documentation pass** (~2 hours) — Add function-level comments to complex GPU kernels
2. **More constexpr** (~1-2 hours) — Look for more opportunities (lookup tables, simple accessors)
3. **Fix CUDA deprecation warnings** (~1 hour) — Replace deprecated intrinsics

---

The code is ours now. The dead weight is gone, the names make sense, and the path forward is clear. We're not rewriting for elegance — we're rewriting for ownership, understanding, and the ability to confidently modify any part of the system.*
