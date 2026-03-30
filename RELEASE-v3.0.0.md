# n0s-ryo-miner v3.0.0 — Modern C++ Rewrite Complete 🚀

**March 30, 2026**

This release marks the **completion of the foundational C++ modernization** of n0s-ryo-miner, transforming the inherited xmr-stak codebase into a clean, maintainable, single-purpose CryptoNight-GPU miner for Ryo Currency.

---

## 🎯 What Changed

### **Complete Codebase Transformation**
- **~320 files modified**, net **-10,530 lines removed** (from ~43K → ~16.5K lines of our code)
- **Zero C files** — Pure modern C++17
- **Linux-only** — Windows/macOS/BSD code stripped
- **Single-purpose** — CryptoNight-GPU only (multi-algo overhead removed)
- **Zero-warning build** (`-Wall -Wextra`)

### **Structural Improvements**
- **Namespace migration**: `xmrstak::` → `n0s::` everywhere
- **Directory restructure**: `xmrstak/` → `n0s/` with clean module separation
- **Smart pointers**: Replaced raw `new`/`delete` with RAII patterns
- **Modern casts**: C++ casts throughout (except device code + macro compat)
- **`constexpr`**: Compile-time evaluation for algorithm lookups and config
- **`[[nodiscard]]`**: 40+ critical functions now enforce error checking

### **Backend Modularization**
- **AMD OpenCL backend**: Monolithic `gpu.cpp` (1003 lines) → 4 focused modules
- **NVIDIA CUDA backend**: Monolithic `cuda_kernels.cu` (832 lines) → 4 focused modules
- **Kernel organization**: Phase-based structure with clear responsibilities
- **Fixed CUDA linkage**: Phase 2+3 kernels in dedicated `.cu`, device functions properly `inline`

---

## 🏗️ Build System Enhancements

### **Container Build Matrix** (NEW)
All builds validated in clean container environments:

| **Backend**       | **Image**                          | **Architectures**                            | **Binary Size** | **Status** |
|-------------------|------------------------------------|----------------------------------------------|-----------------|------------|
| **OpenCL**        | `ubuntu:22.04`                     | AMD GCN/RDNA/CDNA                            | 589K + 767K lib | ✅          |
| **CUDA 11.8**     | `nvidia/cuda:11.8.0-devel-ubuntu22.04` | Pascal/Turing/Ampere (sm_61/75/80/86/89)    | 589K + 2.8M lib | ✅          |
| **CUDA 12.6**     | `nvidia/cuda:12.6.0-devel-ubuntu22.04` | Pascal-Hopper (sm_61-90)                     | 589K + 3.2M lib | ✅          |
| **CUDA 12.8**     | `nvidia/cuda:12.8.0-devel-ubuntu22.04` | Pascal-Blackwell (sm_61/75/80/86/89/90/100/120) | 589K + 3.9M lib | ✅          |

**Scripts:**
- `scripts/container-build-opencl.sh` — OpenCL builds
- `scripts/container-build.sh` — CUDA builds (11.8/12.6/12.8)
- `scripts/build-matrix.sh` — Run full matrix test

---

## ✅ Quality & Testing

### **Validation**
- ✅ **Golden hash tests pass** (3/3 known-good test vectors)
- ✅ **Production miner validated** (initializes GPUs, compiles kernels, runs cleanly)
- ✅ **Zero crashes** on AMD RDNA4 (gfx1201) OpenCL backend
- ✅ **Bit-exact output** preserved throughout refactoring

### **Memory Safety**
- ✅ All `minethd` memory leaks fixed (→ `unique_ptr<iBackend>`)
- ✅ Socket leaks fixed (`unique_ptr<base_socket>`)
- ✅ PIMPL patterns modernized (5x `opaque_private` → `unique_ptr`)
- 7 remaining raw `new` are intentional singletons (executor, jconf, etc.)

---

## 📦 Artifacts

Pre-built binaries in `dist/`:

```
dist/
├── opencl-ubuntu22.04/
│   ├── n0s-ryo-miner              (589K)
│   └── libn0s_opencl_backend.so   (767K)
├── cuda-11.8/
│   ├── n0s-ryo-miner              (589K)
│   └── libn0s_cuda_backend.so     (2.8M)
├── cuda-12.6/
│   ├── n0s-ryo-miner              (589K)
│   └── libn0s_cuda_backend.so     (3.2M)
└── cuda-12.8/
    ├── n0s-ryo-miner              (589K)
    └── libn0s_cuda_backend.so     (3.9M)
```

---

## 🛠️ Build from Source

### **Requirements**
- **OpenCL builds**: `ocl-icd-opencl-dev`, `opencl-headers` (Ubuntu 22.04+)
- **CUDA builds**: NVIDIA CUDA Toolkit 11.8+ (for Pascal/Turing/Ampere/Hopper/Blackwell)
- **Compiler**: GCC 7+ or Clang 6+
- **CMake**: 3.18+

### **Quick Build**
```bash
# OpenCL (AMD GPUs)
cmake -B build -DCUDA_ENABLE=OFF -DCPU_ENABLE=OFF
cmake --build build -j$(nproc)

# CUDA (NVIDIA GPUs, auto-detect architectures)
cmake -B build -DOPENCL_ENABLE=OFF -DCPU_ENABLE=OFF
cmake --build build -j$(nproc)
```

### **Container Builds** (Recommended for Reproducibility)
```bash
# Full matrix test
./scripts/build-matrix.sh

# Individual builds
./scripts/container-build-opencl.sh          # OpenCL
./scripts/container-build.sh 11.8 61,75,86  # CUDA 11.8 (specific archs)
./scripts/container-build.sh 12.8 auto      # CUDA 12.8 (all supported archs)
```

---

## 🚀 What's Next

### **Optimization Phase** (Upcoming)
Now that the foundation is solid, we can confidently optimize:

1. **Profiling**: Baseline hashrate measurements on real hardware
2. **Autotuning**: Algorithm/kernel parameter optimization (see `docs/PRD_01-AUTOTUNING.md`)
3. **Memory patterns**: Optimize shared memory usage in Phase 3 kernel
4. **Occupancy**: GPU occupancy improvements for better parallelism

### **Future Enhancements**
- [ ] Live pool mining validation (needs wallet/pool)
- [ ] Benchmark harness refinement (current: production miner works, harness has environmental issues)
- [ ] CI/CD integration (automated build matrix testing)
- [ ] Performance regression tracking

---

## 📝 Breaking Changes

### **Removed Features**
- ❌ **Multi-algorithm support** — CryptoNight-GPU only
- ❌ **CPU mining** — Reference impl remains for golden hash tests only
- ❌ **Windows/macOS/BSD** — Linux-only
- ❌ **hwloc** — Optional dependency removed from default builds

### **Config Changes**
- Simplified config parser (single-algorithm focus)
- Hardcoded OpenCL constants (no runtime tuning — use container builds)

---

## 🙏 Credits

**Original xmr-stak authors** for the foundational GPU mining implementation.

**Session contributors** (Sessions 16-33):
- Smart pointer modernization (S16, S21, S22)
- `[[nodiscard]]` + constexpr expansion (S17, S19, S20)
- AMD/NVIDIA backend modularization (S18, S19)
- CUDA kernel linkage fixes (S22, S31)
- Container build infrastructure (S31, S32)
- Production validation + release prep (S30, S33)

---

## 📊 Statistics

- **Commits**: 15 major refactoring commits
- **Sessions**: 18 active development sessions (S16-S33)
- **Files changed**: ~320
- **Net lines removed**: -10,530+
- **Final codebase**: ~16,550 lines (down from ~43K original)
- **Test coverage**: 3 golden hash test vectors (100% pass)
- **Build matrix**: 4 targets, 100% pass rate

---

**This release represents the completion of the foundational rewrite phase. The codebase is now clean, modern, maintainable, and ready for optimization work.** 🎉

For detailed session-by-session progress, see `docs/REWRITE-PLAN.md`.
