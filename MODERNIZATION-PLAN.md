# n0s-ryo-miner Modernization Plan

**Phase 3:** Multi-platform builds, CUDA 12.x compatibility, code modernization.
**Phases 1 & 2:** Complete — see REFACTOR-PLAN-V2.md for history.

---

## Hardware Inventory

| Host | GPU | Arch | Compute Cap | OS | CUDA Toolkit | Driver | Driver CUDA Cap |
|------|-----|------|-------------|-------|-------------|--------|-----------------|
| nitro | AMD RX 9070 XT | RDNA 4 (gfx1201) | N/A | Ubuntu 24.04 (noble) | N/A | ROCm 7.2 | N/A |
| nos2 | NVIDIA GTX 1070 Ti | Pascal (GP104) | 6.1 | Ubuntu 22.04 (jammy) | CUDA 11.8 | 580.126.09 | CUDA 13.0 |
| nosnode | NVIDIA RTX 2070 | Turing (TU106) | 7.5 | Ubuntu 22.04 (jammy) | CUDA 12.6 | 535.288.01 | CUDA 12.2 |

### Key Observations
- **nos2** has driver 580 (CUDA 13.0 capable!) but only CUDA 11.8 toolkit installed
- **nosnode** has CUDA 12.6 toolkit but driver 535 only supports runtime CUDA 12.2
- **nosnode** is missing `cmake` — needs install
- Both NVIDIA hosts are Ubuntu 22.04 (jammy)

---

## CUDA / GPU Architecture Compatibility Matrix

### Compute Capabilities by Architecture

| Architecture | Compute Cap | Min CUDA Version | Example GPUs |
|-------------|------------|-------------------|--------------|
| Pascal | 6.0, 6.1, 6.2 | CUDA 8.0 | GTX 1060/1070/1080, P100 |
| Volta | 7.0, 7.2 | CUDA 9.0 | V100, Titan V |
| Turing | 7.5 | CUDA 10.0 | RTX 2060/2070/2080, T4 |
| Ampere | 8.0, 8.6, 8.7 | CUDA 11.0 (8.0), 11.1 (8.6) | A100, RTX 3060/3070/3080/3090 |
| Ada Lovelace | 8.9 | CUDA 11.8 | RTX 4060/4070/4080/4090 |
| Hopper | 9.0 | CUDA 12.0 | H100, H200 |
| Blackwell | 10.0, 10.1, 12.0 | CUDA 12.8 | B100, B200, RTX 5090 |

### CUDA Toolkit → Deprecated/Dropped Architectures

| CUDA Version | Dropped | Deprecated | Max SM Supported |
|-------------|---------|------------|------------------|
| 11.8 | Kepler (sm_30-37) | Maxwell (sm_50-53) | sm_89 (Ada) |
| 12.0-12.6 | Maxwell (sm_50-53) | — | sm_90 (Hopper) |
| 12.8 | — | — | sm_120 (Blackwell) |
| 13.0+ | — | — | sm_120+ |

### Our Target Build Matrix

| CUDA Version | Target Archs (sm_) | Cards Covered |
|-------------|-------------------|---------------|
| 11.8 | 61, 75, 80, 86, 89 | Pascal, Turing, Ampere, Ada |
| 12.6 | 61, 75, 80, 86, 89, 90 | Pascal through Hopper |
| 12.8+ | 61, 75, 80, 86, 89, 100, 120 | Pascal through Blackwell |

**Note:** sm_30/35/37/50/52 dropped — these are ancient Kepler/Maxwell cards. Our minimum is Pascal (sm_60).

### CUDA Toolkit → Ubuntu Support

| CUDA Version | Ubuntu 18.04 (bionic) | Ubuntu 20.04 (focal) | Ubuntu 22.04 (jammy) | Ubuntu 24.04 (noble) |
|-------------|----------------------|---------------------|---------------------|---------------------|
| 11.8 | ✅ | ✅ | ✅ | ❌ |
| 12.0-12.4 | ❌ | ✅ | ✅ | ❌ |
| 12.6 | ❌ | ❌ | ✅ | ✅ |
| 12.8+ | ❌ | ❌ | ✅ | ✅ |

---

## Phase 3.1: CUDA 12.x Compatibility Fix (CRITICAL PATH)

**Problem:** Code uses deprecated CUDA intrinsics removed in CUDA 12.x:
- `int2float()` → `__int2float_rn()`
- `float_as_int()` → `__float_as_int()`
- `int_as_float()` → `__int_as_float()`

**Also:** Default CUDA arch list includes sm_30/35/37/50/52 which are dropped in CUDA 12.x.

### Tasks:
- [ ] 3.1.1: Add CUDA 12.x compat shims for deprecated intrinsics
- [ ] 3.1.2: Modernize default CUDA arch list (drop pre-Pascal, add Ampere/Ada/Blackwell)
- [ ] 3.1.3: Raise minimum CUDA version from 7.5 to 11.0
- [ ] 3.1.4: Install cmake on nosnode, test CUDA 12.6 build
- [ ] 3.1.5: Test on nos2 (CUDA 11.8, Pascal) — verify backward compat
- [ ] 3.1.6: Test on nosnode (CUDA 12.6, Turing) — verify forward compat

### Implementation:
Add a compat header `cuda_compat.hpp` with version-gated shims:
```cuda
#pragma once
#include <cuda_runtime.h>

// CUDA 12.x removed these shorthand intrinsics
#if CUDART_VERSION >= 12000
  #define int2float(x)     __int2float_rn(x)
  #define float_as_int(x)  __float_as_int(x)
  #define int_as_float(x)  __int_as_float(x)
#endif
```

---

## Phase 3.2: CMake Modernization

### Tasks:
- [ ] 3.2.1: Raise cmake_minimum_required to 3.18 (for native CUDA language support)
- [ ] 3.2.2: Migrate from `find_package(CUDA)` + `cuda_add_library()` to CMake native CUDA
- [ ] 3.2.3: Use `CMAKE_CUDA_ARCHITECTURES` instead of manual gencode flags
- [ ] 3.2.4: Upgrade C++ standard from C++11 to C++17
- [ ] 3.2.5: Clean up dead CMake paths (Fermi/Kepler guards, CUDA <8 workarounds, APPLE/MSVC cruft)

### Why:
- `find_package(CUDA)` / `cuda_add_library()` are deprecated since CMake 3.10
- Native CUDA support is cleaner, faster, and better integrated
- C++17 gives us `std::optional`, `std::string_view`, structured bindings, `if constexpr`
- Removes hundreds of lines of version-check spaghetti

---

## Phase 3.3: Code Modernization (C++17)

### Tasks:
- [ ] 3.3.1: Replace raw `new`/`delete` with smart pointers where safe
- [ ] 3.3.2: Use `std::string_view` for read-only string params
- [ ] 3.3.3: Modernize error handling patterns
- [ ] 3.3.4: Clean up include graph (remove unused headers)
- [ ] 3.3.5: Add `[[nodiscard]]` to functions with important return values
- [ ] 3.3.6: Strip remaining dead code (CryptonightR codegen files, etc.)

---

## Phase 3.4: Dead Code Removal (Remaining)

Files/code that exist but are dead after Phase 2 cleanup:

- [ ] `xmrstak/backend/nvidia/CudaCryptonightR_gen.cpp/hpp` — CryptonightR runtime codegen (dead)
- [ ] `xmrstak/backend/amd/OclCryptonightR_gen.cpp/hpp` — CryptonightR OpenCL codegen (dead)
- [ ] `xmrstak/backend/cpu/crypto/variant4_random_math.h` — CryptonightR math (dead)
- [ ] `xmrstak/backend/cpu/crypto/CryptonightR_gen.cpp` — CryptonightR CPU codegen (dead)
- [ ] `xmrstak/backend/cpu/crypto/asm/cnR/` — CryptonightR ASM templates (dead)
- [ ] `xmrstak/backend/nvidia/nvcc_code/cuda_fast_div_heavy.hpp` — heavy algo helper (dead)
- [ ] `xmrstak/backend/nvidia/nvcc_code/cuda_fast_int_math_v2.hpp` — v2 algo helper (dead)
- [ ] `xmrstak/backend/amd/amd_gpu/opencl/fast_div_heavy.cl` — heavy OpenCL helper (dead)
- [ ] `xmrstak/backend/amd/amd_gpu/opencl/fast_int_math_v2.cl` — v2 OpenCL helper (dead)
- [ ] `.appveyor.yml`, `.travis.yml` — dead CI configs (we don't use these)

---

## Phase 3.5: Build & Test Infrastructure

### Tasks:
- [ ] 3.5.1: Create `scripts/build.sh` — unified build script with presets
- [ ] 3.5.2: Create `scripts/test-cuda12.sh` — nosnode-specific test
- [ ] 3.5.3: Update `test-mine-remote.sh` to support nosnode as REMOTE
- [ ] 3.5.4: GitHub Actions CI (build matrix: CUDA 11.8, 12.6 × Ubuntu 22.04, 24.04)

---

## Execution Order

1. **Phase 3.1** (CUDA 12.x compat) — FIRST, unblocks nosnode testing
2. **Phase 3.4** (Dead code removal) — Clean house before modernizing
3. **Phase 3.2** (CMake modernization) — Foundation for everything else
4. **Phase 3.3** (C++17 modernization) — Code quality
5. **Phase 3.5** (Build infrastructure) — Automation

---

## Testing Protocol (Updated)

### Three-way test:
```bash
# AMD (nitro) — local OpenCL
./test-mine.sh

# NVIDIA Pascal (nos2) — remote CUDA 11.8
REMOTE=nos2 ./test-mine-remote.sh

# NVIDIA Turing (nosnode) — remote CUDA 12.6
REMOTE=nosnode ./test-mine-remote.sh
```

### Before every commit:
- Build succeeds on local (AMD)
- At least one NVIDIA build verified

### Before every merge to master:
- All three platforms tested

---

## Success Criteria

**Phase 3 Complete When:**
- [ ] Builds with CUDA 11.8 (Pascal, nos2)
- [ ] Builds with CUDA 12.6 (Turing, nosnode)
- [ ] Builds with ROCm/OpenCL (RDNA 4, nitro)
- [ ] CMake uses native CUDA language support
- [ ] C++17 standard enforced
- [ ] All dead code removed
- [ ] No deprecated CUDA API usage
- [ ] Default arch list covers Pascal → Ada (Blackwell when CUDA 12.8+)

---

*Phase 3 transforms this from "it works on our exact machines" to "it works across the NVIDIA ecosystem."*
