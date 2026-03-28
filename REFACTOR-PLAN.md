# n0s-cngpu Refactor Plan

**Project:** Fork of xmr-stak → n0s-cngpu (dedicated CryptoNight-GPU miner)
**Started:** 2026-03-28
**Estimated Duration:** 2-3 weeks
**License:** GPLv3 (inherited from xmr-stak)

---

## Executive Summary

Strip xmr-stak down to a single-purpose CryptoNight-GPU miner for RYO Currency. Remove ~70% of dead algorithm code, all developer fee infrastructure, and the entire NVIDIA/CUDA backend. Rebrand as `n0s-cngpu`, add proper CI/CD with multi-distro Podman testing, and ship clean release builds.

---

## Phase 1: Dev Fee Removal & Dead Code Purge (Days 1-3)
**Branch:** `phase1/fee-removal-cleanup`

### Task 1.1: Remove Developer Fee System
- [ ] Delete `xmrstak/donate-level.hpp`
- [ ] Remove `fDevDonationLevel` references from:
  - `xmrstak/version.hpp` (version string appends donation level)
  - `xmrstak/version.cpp`
  - `xmrstak/cli/cli-miner.cpp` (prints donation percentage)
  - `xmrstak/misc/executor.hpp` (donation period calculation)
  - `xmrstak/misc/executor.cpp` (dev pool logic, donate.xmr-stak.net connections)
- [ ] Remove `is_dev_pool()` / `pool` bool from `xmrstak/net/jpsock.hpp` and `.cpp`
- [ ] Remove dev pool constructor param from `jpsock`
- [ ] Remove dev pool switching logic from `executor.cpp` (lines ~570-610 donate pool URLs, all is_dev_pool branches)
- [ ] Remove dev pool wallet addresses from all files
- [ ] Clean up `pool_coin[2]` → single pool in `coinDescription.hpp`

### Task 1.2: Remove Non-CNGPU Algorithm Code
- [ ] Strip `xmrstak_algo_id` enum down to `invalid_algo` + `cryptonight_gpu` only
- [ ] Remove all non-GPU entries from `coins[]` array in `jconf.cpp`
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

### Task 1.3: Remove NVIDIA/CUDA Backend Entirely
- [ ] Delete entire `xmrstak/backend/nvidia/` directory (23 files)
- [ ] Remove CUDA_ENABLE from CMakeLists.txt
- [ ] Remove CUDA find_package, build targets, and link libraries
- [ ] Remove nvidia from BACKEND_TYPES
- [ ] Remove .cu file compilation rules
- [ ] Update backend connector to not reference nvidia

**Validation:** Build with `cmake -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON .. && make` — must compile clean and mine cn-gpu.

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
  - Keep all existing entries (wolf9466 AMD, RapidJSON, PicoSHA2, cpputil)
  - Remove NVIDIA reference (backend deleted)
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

### Task 3.2: OpenCL Build Testing
- [ ] Add OpenCL variant Containerfiles (at least for jammy + noble with ROCm)
- [ ] `Containerfile.noble-opencl` — install ROCm OpenCL dev headers
- [ ] Test that OpenCL backend compiles (can't test GPU at container runtime without GPU passthrough, but compilation must succeed)

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
  - Additional job: noble-opencl
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
| Phase 1: Fee Removal & Code Purge | 🔴 | `phase1/fee-removal-cleanup` | Start here |
| Phase 2: Rebrand | 🔴 | `phase2/rebrand` | Depends on Phase 1 |
| Phase 3: Podman Test Harness | 🔴 | `phase3/test-harness` | Can partially parallel with Phase 2 |
| Phase 4: CI/CD Pipeline | 🔴 | `phase4/ci-cd` | Depends on Phase 3 |
| Phase 5: Documentation | 🔴 | `phase5/docs` | Can start during Phase 3 |

### Session Notes
_(Updated by cron sessions as work progresses)_

**2026-03-28:** Initial plan created. Codebase audited:
- ~40K lines of C/C++ code
- 18 algorithm variants, only keeping 1 (cryptonight_gpu)
- NVIDIA backend: 23 files to delete entirely
- Dev fee: touches 8 files, ~100 lines of pool switching logic
- GPU OpenCL kernels: 9 .cl files, only 1 needed (cryptonight_gpu.cl + its deps)
- License: GPLv3, must keep + add fork copyright notice
