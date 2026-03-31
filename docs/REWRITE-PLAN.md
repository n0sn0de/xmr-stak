# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Foundation + dead code removal complete. CUDA consolidated. Namespace migrated (n0s::). Pool/network documented. Directory restructured (xmrstak/ → n0s/). Zero-warning build. Modern C++ patterns applied. Config/algo simplified. OpenCL constants hardcoded. Windows/macOS/BSD code stripped. Pure C++17 (zero C files). Linux-only. Smart pointers replacing raw new/delete. std::regex eliminated from hot paths. Pre-sm_60 CUDA dead code purged (704 lines). Ancient GCN 1.0 OpenCL workarounds removed.*

---

## Goal

Take the inherited xmr-stak CryptoNight-GPU implementation and transform it into a clean, modern, single-purpose miner we can reason about, optimize, and maintain confidently. The algorithm math remains bit-exact — we change organization and expression, not computation. Then we optimize for cn-gpu hashrate performance across AMD & Nvidia architectures.

---

## Branch Strategy:

1. Master stays stable (release-ready always)
2. Focused optimization branches (optimize/profiling-baseline, etc.)
3. Test: golden hashes + container builds + live mining
4. Merge only when validated ✅

## Optimization Principles:

1. Measure before changing
2. One optimization at a time
3. Bit-exact validation always
4. Document findings
5. Keep it clean

## Modernization Principals
1. **Bit-exact output** — Verified via golden test vectors + live mining on 3 GPUs
2. **One module at a time** — Build → test → verify → merge → delete branch → repeat
3. **Test-driven** — Validation harness with known-good hashes before any code change
4. **No magic** — Every function, constant, and parameter has a clear name and purpose
5. **Modern idioms** — `constexpr`, proper naming, RAII, documentation

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
├── autotune/              ← Autotuning framework (Session 40-41, ~2,600 lines)
│   ├── autotune_types.hpp       ← Core types: fingerprint, candidates, scores, state
│   ├── autotune_score.hpp       ← Scoring model + guardrail rejection
│   ├── autotune_candidates.hpp  ← Candidate generation (AMD + NVIDIA)
│   ├── autotune_persist.cpp/hpp ← JSON persistence (autotune.json)
│   ├── autotune_manager.cpp/hpp ← Orchestrator (coarse→stability→winner)
│   ├── autotune_runner.cpp/hpp  ← Subprocess benchmark evaluator + fingerprinting
│   └── autotune_entry.cpp/hpp   ← Top-level entry point + config writing
│
├── cli/cli-miner.cpp     ← Entry point (1012 lines, +65 for autotune CLI)
├── jconf.cpp/hpp          ← Main config (727 lines)
├── params.hpp             ← CLI parameters (+13 autotune params)
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
├── test_autotune.cpp      ← Autotune framework unit tests (20 tests)
├── build_harness.sh       ← Build script
└── build_autotune_test.sh ← Autotune test build script
```
---

## Remaining Work

### Near-Term Opportunities

**Next Session Targets (Post-Session 32):**
1. ✅ **Container build matrix** — DONE (S31: OpenCL + CUDA 11.8; S32: Full matrix 11.8/12.6/12.8)
2. ✅ **Full matrix test** — DONE (S32: All 4 builds pass, 100% success rate)
3. ✅ **Branch cleanup & merge** (~15 min) — Merge refactor/benchmark-phase1 to master, delete stale branches
4. ✅ **Create release** (~15 min) — Tag version, package dist/ artifacts for GitHub release
5. ✅ **Documentation update** — README overhaul + AUTOTUNE.md + CHANGELOG.md (S43)
6. ✅ **Live mining validation** — 100% accepted shares on 3 GPUs (S44)
7. ✅ **Begin optimization phase** — Profiled all phases, autotune implemented (S37-42)

**Deferred (revisit during optimization phase):**
- ~~Benchmark harness debugging~~ — Production miner works, harness has environmental issues
- Use production miner for hashrate measurements until harness is fixed

### Performance Optimization (P1)
- ⏳ Deeply review the CN-GPU-WHITEPAPER.md to understand the algo in conjunction with the codebase to assess approach to optimization opportunities
- ✅ Profile on AMD RDNA4 (S37: --profile flag, per-phase timing, baseline established)
- ✅ Profile on NVIDIA Pascal/Turing (S38: --profile ported to CUDA, baselines established)
- ⏳ Optimize shared memory usage in Phase 3 kernel
- ⏳ Explore occupancy improvements
- ⏳ Consider CUDA Graphs for kernel chaining
- ✅ Algorithm/Kernel Autotuning framework — Phase 1 complete (S40: types, scoring, candidates, persistence, CLI, 20 unit tests)
- ✅ Algorithm/Kernel Autotuning — Phase 2: End-to-end `--autotune` (S41: subprocess benchmark eval, fingerprinting, config writing, 3-GPU validated)
- ⏳ Profile hotspots on AMD RDNA4 + NVIDIA Pascal/Turing/Ampere
- ✅ Fix or rebuild benchmark harness for reproducible measurements (S36: --benchmark-json + stability CV% + tests/benchmark.sh)

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

---

## Session 44 Notes (2026-03-30 11:01 PM) — Live Mining Validation + Release Binaries 🎯🚀

**What we accomplished:**
- ✅ **Container build matrix — ALL 4 backends pass:**
  - OpenCL (Ubuntu 22.04): 1.6 MiB — verified 4,506 H/s benchmark
  - CUDA 11.8 (Pascal→Ada): 3.6 MiB
  - CUDA 12.6 (Pascal→Hopper): 4.0 MiB
  - CUDA 12.8 (Pascal→Blackwell): 4.7 MiB
- ✅ **LIVE MINING — 100% accepted shares on all 3 GPUs:**
  - **AMD RX 9070 XT** (nitro): 120 seconds, ~200+ shares accepted, diff 1000→1500→2250, **zero rejections**
  - **NVIDIA GTX 1070 Ti** (nos2): 60 seconds, 72 shares accepted, CUDA 11.8 container binary, **zero rejections**
  - **NVIDIA RTX 2070** (nosnode): 60 seconds, 88 shares accepted, CUDA 12.6 binary in forward-compat mode (driver 12.2), **zero rejections**
- ✅ **All 8 release binaries uploaded to GitHub** (v3.1.0 release)
- ✅ **Autotuned configs validated in production** — pool difficulty ramp confirms real hashrate

**This is the first time the entire pipeline has been validated end-to-end:**
`autotune → config write → container build → deploy → live mine → 100% accepted shares`

**Next session priorities (Session 45):**
1. **Autotune balanced mode on NVIDIA** — Test wider block count sweep
2. **Phase 4+5 optimization experiments** — Profile AES rounds on 3 GPUs
3. **Multi-GPU autotune** — Test `--autotune-gpu 0,1` scenarios
4. **Interactive hashrate display** — Periodic GPU telemetry updates during mining

---

## Session 44b Notes (2026-03-30 11:35 PM) — Colorized Terminal + GPU Telemetry 🎨⚡

**What we accomplished:**
- ✅ **RYO-branded ASCII banner** — Blue→cyan gradient using 256-color ANSI codes
  - "N0S" in medium blue, "RYO" in cyan, connected with bright cyan dash
  - Version + tagline in dim text, framed in dark blue box-drawing
- ✅ **Colorized share notifications:**
  - `✓` green checkmark for accepted shares
  - `✗` red X for rejected shares
  - Pool address in dim text
- ✅ **GPU telemetry module** — `gpu_telemetry.cpp/hpp`:
  - AMD: sysfs queries for temp, power (µW→W), fan RPM/%, GPU/mem clocks
  - NVIDIA: nvidia-smi CSV query for temp, power, fan%, GPU/mem clocks
  - Color-coded temperature: cyan (<70°C), yellow (70-85°C), red (>85°C)
  - H/W efficiency display (hashrate / watts)
- ✅ **Extended ANSI color palette** — K_BRIGHT_BLUE, K_BRIGHT_CYAN, K_DIM, K_BOLD
- ✅ **Colorized hashrate report** — Gradient separators, colored totals, telemetry section
- ✅ **Color-stripped logfile output** — `print_str_color()` method
- ✅ **3-GPU build validated** — nitro (OpenCL), nos2 (CUDA 11.8), nosnode (CUDA 12.6)
- ✅ **Live mining: green checkmarks flooding in** — 100% accepted shares with new visuals

---

The code is ours now. The dead weight is gone, the names make sense, and the path forward is clear. We're not rewriting for elegance — we're rewriting for ownership, understanding, and the ability to confidently modify any part of the system.

---

## Session 45 Notes (2026-03-31 12:00 PM) — Dead Architecture Code Purge 🧹⚡

**What we accomplished:**
- ✅ **Purged 704 lines of dead pre-sm_60 CUDA code:**
  - Removed `cuda_cryptonight_r.curt` (618 lines, completely unreferenced dead file)
  - Stripped all `__CUDA_ARCH__ < 300/350/500` fallback paths (min target is sm_60 Pascal)
  - Removed pre-CUDA 9 `__shfl()` paths → always `__shfl_sync()` 
  - Simplified `shuffle<>` template: removed unused shared memory params (was pre-sm_30 fallback)
  - Removed `N0S_LARGEGRID` cmake conditional → always `uint64_t` IndexType
  - Always use `__byte_perm()`, `__funnelshift_l/r()` for bit ops (native on sm_60+)
  - Always use `__syncwarp()` (CUDA 9+ guaranteed)
  - Removed dynamic shared memory allocation for shuffle fallback in Phase 4 dispatch
  - **Kept** `__CUDA_ARCH__ < 700` `.cg` cache hints (real perf benefit on Pascal/Turing)
- ✅ **Removed dead Tahiti/Pitcairn OpenCL code path** (ancient GCN 1.0, HD 7000 series from 2012)
- ✅ **3-GPU validation — all pass with identical hashrates:**
  - AMD RX 9070 XT (nitro): 2304.0 H/s ✅
  - NVIDIA GTX 1070 Ti (nos2): 1489.6 H/s ✅  
  - NVIDIA RTX 2070 (nosnode): 2073.6 H/s ✅
- ✅ **Both branches merged to master, deleted, all 3 nodes synced**

**Key insight:** The CUDA codebase carried ~700 lines of dead compatibility code for architectures (Fermi/Kepler/Maxwell) that our CMakeLists.txt already rejects at configure time (min sm_60). The `cuda_cryptonight_r.curt` file was a 618-line template for a completely different algorithm variant that was never compiled or referenced. Clean kills, zero risk.

**What we deliberately kept:**
- `__CUDA_ARCH__ < 700` cache hint paths: `.cg` (cache global L2 only) PTX for `loadGlobal64/32` and `storeGlobal64/32` — real optimization for Pascal/Turing scratchpad access
- `HAS_AMD_BPERMUTE` OpenCL paths: AMD's equivalent of CUDA `__shfl_sync`, used on GCN 3+ and RDNA
- `COMP_MODE` OpenCL paths: runtime-configured for non-aligned workgroup sizes

**Next session priorities (Session 46):**
1. ✅ **Phase 4+5 profiling split** — Separate Phase 4 (implode) from Phase 5 (finalize) timing
2. ✅ **Phase 4+5 optimization experiments** — Tested AES table variants, worksize tuning
3. ✅ **RDNA worksize fix** — Default worksize 16 for RDNA GPUs (was 8)

---

## Session 46 Notes (2026-03-31 12:14 PM) — Phase 4/5 Profiling Split + RDNA Worksize Optimization 🔬⚡

**What we accomplished:**

### 1. Phase 4/5 Profiling Split (CUDA)
- ✅ Added separate `phase4_us` and `phase5_us` fields to `KernelProfile`
- ✅ CUDA dispatch now times Phase 4 (implode) separately from Phase 5 (finalize)
- ✅ Phase 5 timing captured via `chrono` in CUDA `minethd.cpp` profile path
- ✅ Updated OpenCL `KernelProfile` struct for field consistency

**Key discovery: Phase 5 is negligible (~270 µs, <0.04%).** All Phase 4+5 time is Phase 4 (AES scratchpad compression).

| GPU | Phase 4 (Implode) | Phase 5 (Finalize) |
|-----|------------------:|-------------------:|
| GTX 1070 Ti | 102,584 µs (15.0%) | 263 µs (0.0%) |
| RTX 2070 | 91,019 µs (11.6%) | 288 µs (0.0%) |

### 2. Corrected Baselines — Old Numbers Were Wrong
- Re-tested at Session 37 commit: **same ~2,380 H/s** (not 4,427 H/s reported)
- Environment changed (AMD driver update), NOT a code regression
- **Phase 4+5 is actually 32-38% of total time, not 18%** — a MUCH bigger target

### 3. Phase 4 Optimization Experiments (AMD RDNA4)
- ❌ **Single AES table** (1KB LDS vs 4KB): No measurable difference — LDS not the bottleneck
- ❌ **Scratchpad prefetching**: ~5% Phase 4 improvement but within noise overall
- ❌ **AES unroll 5 vs 10**: Partial unroll is slower — full unroll is optimal
- ✅ **Worksize 16 vs 8**: **4.4% hashrate improvement**, Phase 4+5 down 42%

### 4. RDNA Worksize Default Fix
- ✅ `autoAdjust.hpp` now detects RDNA (gfx1xxx prefix) and sets worksize 16
- Previously only VEGA (gfx9xx) got worksize 16; RDNA fell through to default 8
- **Benchmarked on RX 9070 XT (gfx1201):**
  - worksize 8:  2,330 H/s — Phase 4+5: 235K µs
  - worksize 16: 2,432 H/s — Phase 4+5: 137K µs (**42% Phase 4+5 reduction**)
- ✅ Live mining validated: 89+ accepted shares, 0 rejected
- ✅ CUDA builds verified clean on nos2 (Pascal)

### 5. Updated 3-GPU Baselines (Session 46)

| Phase | RX 9070 XT (ws=16) | GTX 1070 Ti | RTX 2070 |
|-------|-------------------:|------------:|---------:|
| Phase 1 | ~6K µs (0.9%) | ~0 µs | ~0 µs |
| Phase 2 | 158K µs (25.2%) | 17K µs (2.5%) | 24K µs (3.1%) |
| Phase 3 | 326K µs (52.0%) | 566K µs (82.5%) | 666K µs (85.2%) |
| Phase 4 | 137K µs (21.9%) | 103K µs (15.0%) | 91K µs (11.6%) |
| Phase 5 | *combined* | 263 µs (0.0%) | 288 µs (0.0%) |
| **Total** | **627K µs** | **686K µs** | **781K µs** |
| **H/s** | **2,432** | **1,552** | **2,211** |

**Next session priorities (Session 47):**
1. **Autotune integration of worksize sweep** — Add worksize 16/32 to AMD candidate generator
2. **Phase 2 investigation on AMD** — Phase 2 jumped to 25% with worksize 16 (was 6% with ws=8)
3. **NVIDIA worksize/block tuning** — Apply similar profiling to CUDA backend
4. **Container builds with worksize fix** — Release v3.2.0 with RDNA optimization
