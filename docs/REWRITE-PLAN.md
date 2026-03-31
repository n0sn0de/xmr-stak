# Backend Rewrite Plan

**High-Level Strategy for the Foundational C++ Rewrite**

*Status: Foundation + dead code removal complete. CUDA consolidated. Namespace migrated (n0s::). Pool/network documented. Directory restructured (xmrstak/ → n0s/). Zero-warning build. Modern C++ patterns applied. Config/algo simplified. OpenCL constants hardcoded. Windows/macOS/BSD code stripped. Pure C++17 (zero C files). Linux-only. Smart pointers replacing raw new/delete. std::regex eliminated from hot paths.*

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
├── autotune/              ← Autotuning framework (Session 40)
│   ├── autotune_types.hpp       ← Core types: fingerprint, candidates, scores, state
│   ├── autotune_score.hpp       ← Scoring model + guardrail rejection
│   ├── autotune_candidates.hpp  ← Candidate generation (AMD + NVIDIA)
│   ├── autotune_persist.cpp/hpp ← JSON persistence (autotune.json)
│   └── autotune_manager.cpp/hpp ← Orchestrator (coarse→stability→winner)
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
5. **Documentation update** (~30 min) — Add container build instructions to README
6. **Live mining validation** (~1 hour when pool/wallet available) — Test accepted shares on real pool
7. **Begin optimization phase** — Profile real mining workload, identify hotspots

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
- ⏳ Algorithm/Kernel Autotuning — Phase 2: Wire backends to evaluator callbacks, implement `--autotune` end-to-end
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

## Session 39 Notes (2026-03-30 06:49 PM) — Phase 3 Optimization Analysis + First Performance Win 🧘

**What we accomplished:**
- ✅ +2.3% hashrate improvement on AMD (4,427.5 → 4,531.0 H/s) via debug printf removal + div→mul optimization
- ✅ Deep CUDA register/shared memory analysis — Phase 3 already at optimal occupancy on Pascal
- ✅ Key insight: algorithm is intentionally optimization-resistant (fma_break, data-dependent addresses)

---

## Session 40 Notes (2026-03-30 08:13 PM) — Autotune Framework (Phase 1 of PRD) 🎵

**What we accomplished:**
- ✅ **Built complete autotune orchestration framework** — `n0s/autotune/` module (6 files, ~1,650 lines)
- ✅ **autotune_types.hpp** — DeviceFingerprint, AmdCandidate, NvidiaCandidate, BenchmarkMetrics, CandidateScore, AutotuneState, AutotuneResult
- ✅ **autotune_score.hpp** — Scoring model: steady hashrate base, stability penalty (CV-based), error penalty (invalid shares, backend errors), guardrail rejection (>5% error, >25% CV, zero hashrate)
- ✅ **autotune_candidates.hpp** — Mode-aware candidate generation for AMD (intensity/worksize sweep) and NVIDIA (threads/blocks/bfactor grid), VRAM-bounded, deduped
- ✅ **autotune_persist.cpp/hpp** — Full JSON round-trip via rapidjson for autotune.json, fingerprint-based cache lookup
- ✅ **autotune_manager.cpp/hpp** — Orchestrator: coarse search → stability validation → winner selection, with backend callbacks (CandidateEvaluator, FingerprintCollector)
- ✅ **CLI integration** — 10 `--autotune*` flags parsed into params (mode, backend, gpu, reset, resume, benchmark-seconds, stability-seconds, target, export)
- ✅ **20 unit tests** — Scoring, rejection, candidate generation (AMD+NVIDIA), fingerprint compatibility, JSON persistence round-trips (AMD+NVIDIA)
- ✅ **3-GPU build validation** — nitro (OpenCL), nos2 (CUDA 11.8), nosnode (CUDA 12.6) all build clean
- ✅ **All tests pass on all 3 nodes** — 20/20 unit tests + golden hash harness
- ✅ **Merged to master**, branch deleted

**Architecture decisions:**
- Backend-agnostic framework uses callback pattern — AMD and NVIDIA backends will implement CandidateEvaluator to run actual GPU benchmarks
- Fingerprint compatibility ignores miner_version changes (only hardware+driver changes invalidate cache)
- Scoring strictly penalizes instability: even 1% invalid share rate = 50% score hit
- Candidate generation is mode-aware: Quick (5-9 candidates), Balanced (15-25), Exhaustive (50+)

**Next session priorities (Session 41):**
1. **Wire AMD backend to autotune** — Implement CandidateEvaluator for OpenCL: init device → set intensity/worksize → run benchmark → collect metrics. This makes `--autotune` work end-to-end on AMD
2. **Wire NVIDIA backend to autotune** — Same for CUDA: threads/blocks/bfactor evaluation
3. **Config writing** — After tuning, write winning params back to amd.txt / nvidia.txt
4. **Live test on all 3 GPUs** — Full autotune run with real hash verification

---

The code is ours now. The dead weight is gone, the names make sense, and the path forward is clear. We're not rewriting for elegance — we're rewriting for ownership, understanding, and the ability to confidently modify any part of the system.
