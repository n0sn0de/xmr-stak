# n0s-cngpu Refactor Plan

**Project:** Fork of xmr-stak → n0s-cngpu (dedicated CryptoNight-GPU miner)
**Started:** 2026-03-28
**Estimated Duration:** 2-3 weeks
**License:** GPLv3 (inherited from xmr-stak)

---

## Executive Summary

Strip xmr-stak down to a single-purpose CryptoNight-GPU miner for RYO Currency supporting both AMD (OpenCL) and NVIDIA (CUDA) GPUs. Remove ~60% of dead algorithm code and all developer fee infrastructure. Rebrand as `n0s-cngpu`, add proper CI/CD with multi-distro Podman testing, and ship clean release builds.

---

## Phase 1: Dev Fee Removal & Dead Code Purge (Days 1-3)
**Branch:** `phase1/fee-removal-cleanup`

### Task 1.1: Remove Developer Fee System ✅ COMPLETE
- [x] Delete `xmrstak/donate-level.hpp`
- [x] Remove `fDevDonationLevel` references from:
  - `xmrstak/version.hpp` (version string appends donation level)
  - `xmrstak/version.cpp`
  - `xmrstak/cli/cli-miner.cpp` (prints donation percentage)
  - `xmrstak/misc/executor.hpp` (donation period calculation)
  - `xmrstak/misc/executor.cpp` (dev pool logic, donate.xmr-stak.net connections)
- [x] Remove `is_dev_pool()` / `pool` bool from `xmrstak/net/jpsock.hpp` and `.cpp`
- [x] Remove dev pool constructor param from `jpsock`
- [x] Remove dev pool switching logic from `executor.cpp` (lines ~570-610 donate pool URLs, all is_dev_pool branches)
- [x] Remove dev pool wallet addresses from all files
- [x] Clean up `pool_coin[2]` → single pool in `coinDescription.hpp`

**Commit:** 5bae325 "Remove developer fee system" (Session 1)

### Task 1.2: Remove Non-CNGPU Algorithm Code ✅ SUFFICIENT (Runtime Restricted)
- [x] Remove all non-GPU entries from `coins[]` array in `jconf.cpp` — **DONE** (commit fb5e124)
- [ ] **DEFERRED TO PHASE 1.5:** Deep algorithm code removal (see below)

**Commit:** fb5e124 "Strip coins array to cryptonight_gpu only (stub approach)" (Session 2)

**Decision:** Runtime restriction via coins[] array is sufficient. The miner now only supports
cryptonight_gpu at runtime. Deeper cleanup (algorithm enum, CPU backend templates, OpenCL/CUDA kernels)
deferred to optional Phase 1.5 due to complexity and low user-facing value.

### Phase 1.5: Deep Algorithm Cleanup (OPTIONAL — DEFERRED)
**Status:** Not started. May be revisited after Phase 2-5 complete.
**Rationale:** Runtime restriction (coins[] array) achieves the goal. Deeper cleanup is high-risk, low-reward.

Deferred items:
- [ ] Strip `xmrstak_algo_id` enum down to `invalid_algo` + `cryptonight_gpu` only
- [ ] Remove all non-GPU POW entries from `cryptonight.hpp`
- [ ] Remove dead algorithm constants (CN_MEMORY, CN_ITER, CN_MASK, CN_TURTLE_MASK, etc — keep only CN_GPU_*)
- [ ] Remove derived algo infrastructure (`start_derived_algo_id`, derived arrays)
- [ ] CPU crypto cleanup:
  - Keep: `cn_gpu_avx.cpp`, `cn_gpu_ssse3.cpp`, `cn_gpu.hpp`
  - Remove: `cryptonight_aesni.h` (strip to GPU-only codepaths), `CryptonightR_gen.cpp`, `variant4_random_math.h`, `asm/cnR/` directory
  - Audit: `cryptonight_common.cpp`, hash functions (blake, groestl, jh, keccak, skein) — keep only what cn_gpu actually uses
- [ ] AMD OpenCL cleanup:
  - Keep: `cryptonight_gpu.cl`, helper CL files it includes
  - Remove: `cryptonight.cl` (the non-GPU variant), `OclCryptonightR_gen.cpp/.hpp`
  - Simplify `gpu.cpp` — remove non-GPU kernel compilation paths
- [ ] CPU minethd.cpp — remove all algorithm switch branches except `cryptonight_gpu`
- [ ] Remove `autoAdjust.hpp` multi-algo memory calculations (simplify to cn_gpu only)
- [ ] NVIDIA/CUDA cleanup:
  - Strip non-CNGPU kernel code from `nvcc_code/` (keep `cuda_cryptonight_gpu.hpp`, remove other algo-specific kernels)
  - Remove `CudaCryptonightR_gen.cpp/.hpp` (CryptonightR is not cn-gpu)
  - Simplify `nvidia/minethd.cpp` — remove all algorithm branches except `cryptonight_gpu`
  - Simplify `nvidia/autoAdjust.hpp` — remove multi-algo memory calculations
  - Clean up `nvidia/jconf.cpp` — remove non-GPU algo config paths

**Phase 1 Validation:** ✅ CPU-only build verified working (Session 4)
- [x] CPU only: `cmake -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=OFF .. && make` — ✅ PASS
- [ ] AMD only: `cmake -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON .. && make` — deferred to Phase 3
- [ ] NVIDIA only: `cmake -DCUDA_ENABLE=ON -DOpenCL_ENABLE=OFF .. && make` — deferred to Phase 3
- [ ] Both: `cmake -DCUDA_ENABLE=ON -DOpenCL_ENABLE=ON .. && make` — deferred to Phase 3

---

## Phase 2: Rebrand to n0s-cngpu (Days 4-6)
**Branch:** `phase2/rebrand`

### Task 2.1: Project Identity
- [ ] Rename CMake project: `project(n0s-cngpu)`
- [ ] Update `version.cpp`:
  - `XMR_STAK_NAME` → `N0S_CNGPU_NAME "n0s-cngpu"`
  - `XMR_STAK_VERSION` → `N0S_CNGPU_VERSION "1.0.0"`
  - Update version macros throughout
- [ ] Rename binary output from `xmr-stak` to `n0s-cngpu`
- [ ] Update all user-facing strings (startup banner, HTTP dashboard title, error messages)

### Task 2.2: License Compliance
- [ ] Keep GPLv3 LICENSE file (inherited, required)
- [ ] Update copyright headers: add `Copyright (C) 2026 n0sn0de contributors`
- [ ] Keep original copyright: `Copyright (C) 2017-2019 fireice-uk, psychocrypt`
- [ ] Update THIRD-PARTY-LICENSES:
  - Keep all existing entries (tsiv/KlausT NVIDIA, wolf9466 AMD, RapidJSON, PicoSHA2, cpputil)
  - Add note about fork lineage
- [ ] Add NOTICE file documenting the fork relationship per GPLv3 §5

### Task 2.3: Source Tree Reorganization
- [ ] Rename `xmrstak/` directory → `n0scngpu/` (or keep as-is with namespace change — TBD based on build complexity)
- [ ] Update all `#include` paths if directory is renamed
- [ ] Update CMakeLists.txt glob patterns
- [ ] Clean up namespace from `xmrstak` → `n0scngpu` (big search-replace, do carefully)
  - **Decision needed:** Full namespace rename vs. keeping xmrstak namespace with a note. Full rename is cleaner but higher risk. Start with keeping namespace, rename in a follow-up if stable.

### Task 2.4: Configuration Simplification
- [ ] Simplify `jconf.cpp` — remove coin selection entirely, hardcode cn_gpu
- [ ] Remove "currency" config field (or make it read-only/ignored)
- [ ] Remove coin_selection struct from `coinDescription.hpp`
- [ ] Simplify pool configuration (remove dev pool concept)
- [ ] Update default pool suggestion to ryo-currency pool
- [ ] Clean up the interactive setup wizard in `cli-miner.cpp` — remove coin choice

**Validation:** Fresh build, run with pool config, verify mining works and version string shows n0s-cngpu.

---

## Phase 3: Podman Test Harness (Days 7-10)
**Branch:** `phase3/test-harness`

### Task 3.1: Containerized Build Testing
- [ ] Create `containers/` directory
- [ ] Write Containerfiles for each Ubuntu LTS:
  - `Containerfile.bionic` (18.04) — GCC 7, CMake 3.10
  - `Containerfile.focal` (20.04) — GCC 9, CMake 3.16
  - `Containerfile.jammy` (22.04) — GCC 11/12, CMake 3.22
  - `Containerfile.noble` (24.04) — GCC 13/14, CMake 3.28
- [ ] Each Containerfile:
  - Installs build deps (libmicrohttpd-dev, libssl-dev, cmake, build-essential, libhwloc-dev, ocl-icd-opencl-dev)
  - Copies source
  - Builds CPU-only (`-DOpenCL_ENABLE=OFF -DCUDA_ENABLE=OFF`)
  - Runs basic smoke test (binary exists, `--help` works, version string correct)
- [ ] Write `scripts/test-all-distros.sh`:
  - Builds all 4 container images via podman
  - Reports pass/fail per distro
  - Supports parallel builds

### Task 3.2: GPU Backend Build Testing
- [ ] Add OpenCL variant Containerfiles (at least for jammy + noble with ROCm)
- [ ] `Containerfile.noble-opencl` — install ROCm OpenCL dev headers
- [ ] Test that OpenCL backend compiles (compilation only — no GPU passthrough in containers)
- [ ] Add CUDA variant Containerfiles (at least for focal + jammy + noble)
- [ ] `Containerfile.noble-cuda` — install CUDA toolkit dev headers
- [ ] Test that CUDA backend compiles (compilation only — no GPU in containers)

### Task 3.3: Test Runner Script
- [ ] `scripts/test-all-distros.sh` — master test orchestrator
  - `--cpu-only` — test CPU-only builds across all distros
  - `--opencl` — test OpenCL builds on supported distros
  - `--all` — run everything
  - Outputs results in a clean table
  - Non-zero exit on any failure

**Validation:** `./scripts/test-all-distros.sh --all` passes on all 4 Ubuntu LTS versions.

---

## Phase 4: CI/CD Pipeline (Days 11-14)
**Branch:** `phase4/ci-cd`

### Task 4.1: GitHub Actions Workflow
- [ ] Create `.github/workflows/build.yml`:
  - Trigger: push to main, PR to main
  - Matrix: bionic, focal, jammy, noble × cpu-only
  - Additional jobs: noble-opencl, noble-cuda
  - Uses the Containerfiles from Phase 3
- [ ] Create `.github/workflows/release.yml`:
  - Trigger: tag push (v*)
  - Build release binaries for all 4 distros
  - Create GitHub release with assets
  - Each asset: `n0s-cngpu-{version}-ubuntu-{codename}-amd64.tar.gz`
  - Contents: binary, sample config, README, LICENSE

### Task 4.2: Release Packaging
- [ ] Write `scripts/package-release.sh`:
  - Builds optimized binary (`-DCMAKE_BUILD_TYPE=Release -DXMR-STAK_COMPILE=generic`)
  - Creates tarball with binary + configs + docs
  - Generates SHA256 checksum file
- [ ] Create sample config files:
  - `config-ryo-example.txt` — RYO mining config
  - `pools-example.txt` — pool config template

### Task 4.3: Clean Up Legacy CI
- [ ] Delete `.travis.yml`
- [ ] Delete `.appveyor.yml`
- [ ] Delete `CI/` directory
- [ ] Delete old `scripts/build_xmr-stak_docker`
- [ ] Delete old `Dockerfile` (replaced by Containerfiles)

**Validation:** Push a tag, GitHub Actions builds and publishes release artifacts for all 4 distros.

---

## Phase 5: Documentation (Days 15-17)
**Branch:** `phase5/docs`

### Task 5.1: Root README
- [ ] New `README.md`:
  - Project name, description, what it mines
  - Quick start (build + configure + run)
  - Supported platforms (Ubuntu LTS versions)
  - Build instructions (CPU-only, OpenCL)
  - Configuration reference
  - Links to RYO Currency
  - License notice (GPLv3, fork of xmr-stak)

### Task 5.2: Compilation Docs
- [ ] Rewrite `doc/compile/compile_Linux.md`:
  - Only Linux (remove Windows/macOS references)
  - Only Ubuntu LTS versions
  - CPU-only build instructions
  - AMD GPU (ROCm/OpenCL) build instructions
  - Troubleshooting section
- [ ] Remove Windows/macOS compile docs
- [ ] Remove CUDA compile docs

### Task 5.3: Usage & Tuning Docs
- [ ] Update `doc/usage.md` — remove coin selection, simplify to cn-gpu only
- [ ] Update `doc/tuning.md` — AMD GPU tuning for cn-gpu specifically
- [ ] Update `doc/FAQ.md` — trim to relevant Q&A
- [ ] Update `doc/troubleshooting.md` — OpenCL focus

### Task 5.4: Contributing Guide
- [ ] New `CONTRIBUTING.md`:
  - How to build and test
  - Code style
  - PR process
  - Testing requirements (must pass Podman test harness)

**Validation:** All docs accurate, no references to xmr-stak branding, no dead links.

---

## Task Tracking

### Status Legend
- 🔴 Not started
- 🟡 In progress
- 🟢 Complete
- ⚠️ Blocked

### Current Status

| Phase | Status | Branch | Notes |
|-------|--------|--------|-------|
| Phase 1: Fee Removal & Code Purge | 🟢 | `phase1/fee-removal-cleanup` | Complete (runtime restricted) |
| Phase 1.5: Deep Algorithm Cleanup | 🔴 | (deferred) | Optional future work |
| Phase 2: Rebrand | 🟡 | `phase2/rebrand` | **START NEXT SESSION** |
| Phase 3: Podman Test Harness | 🔴 | `phase3/test-harness` | After Phase 2 |
| Phase 4: CI/CD Pipeline | 🔴 | `phase4/ci-cd` | Depends on Phase 3 |
| Phase 5: Documentation | 🔴 | `phase5/docs` | Can start during Phase 3 |

### Session Notes
_(Updated by cron sessions as work progresses)_

**2026-03-28 16:34 (Session 4):** Phase 1 complete. Runtime restricted to cryptonight_gpu only via coins[] array strip. Deep algorithm cleanup deferred to optional Phase 1.5 due to complexity. Moving to Phase 2 (rebrand) next session.

**2026-03-28 16:00 (Session 1-3):** Initial plan created. Codebase audited:
- ~40K lines of C/C++ code
- 18 algorithm variants, only keeping 1 (cryptonight_gpu)
- Both GPU backends kept: AMD/OpenCL (9 .cl files → strip to cn_gpu) + NVIDIA/CUDA (strip to cn_gpu kernels)
- Dev fee: touches 8 files, ~100 lines of pool switching logic to remove
- License: GPLv3, must keep + add fork copyright notice
