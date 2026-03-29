# Build & Test Matrix

**Current validation status of n0s-ryo-miner across platforms.**

*Last updated: 2026-03-29 (Phase 3.8)*

---

## Validated Build Matrix

### NVIDIA CUDA Builds

| CUDA | Ubuntu | cmake | Architectures (sm_) | Cards Covered | Compile | Mine Test |
|------|--------|-------|---------------------|---------------|---------|-----------|
| 11.8 | 18.04 (bionic) | Kitware PPA | 61, 75, 80, 86, 89 | Pascal → Ada | ✅ | — |
| 11.8 | 20.04 (focal) | Kitware PPA | 61, 75, 80, 86, 89 | Pascal → Ada | ✅ | — |
| 11.8 | 22.04 (jammy) | apt | 61, 75, 80, 86, 89 | Pascal → Ada | ✅ | ✅ nos2 (Pascal) |
| 12.6 | 22.04 (jammy) | apt | 61, 75, 80, 86, 89, 90 | Pascal → Hopper | ✅ | ✅ nosnode (Turing) |
| 12.6 | 24.04 (noble) | apt | 61, 75, 80, 86, 89, 90 | Pascal → Hopper | ✅ | — |
| 12.8 | 22.04 (jammy) | apt | 61, 75, 80, 86, 89, 90, 100, 120 | Pascal → Blackwell | ✅ | — |
| 12.8 | 24.04 (noble) | apt | 61, 75, 80, 86, 89, 90, 100, 120 | Pascal → Blackwell | ✅ | — |

### AMD OpenCL Builds

| Ubuntu | Compile | Mine Test |
|--------|---------|-----------|
| 20.04 (focal) | ✅ | — |
| 22.04 (jammy) | ✅ | — |
| 24.04 (noble) | ✅ | ✅ nitro (RX 9070 XT, RDNA 4) |

### Intentionally Skipped Combinations

| Combination | Reason |
|---|---|
| CUDA 11.8 × noble (24.04) | NVIDIA doesn't ship CUDA 11.8 for noble |
| CUDA 12.6+ × bionic (18.04) | NVIDIA dropped bionic support after 11.x |
| CUDA 12.6+ × focal (20.04) | NVIDIA dropped focal for 12.6+ |
| OpenCL × bionic (18.04) | Ubuntu 18.04 is EOL |

---

## Hardware Test Fleet

| Host | GPU | Architecture | CUDA/Driver | OS | Role |
|------|-----|-------------|-------------|-----|------|
| nitro | AMD RX 9070 XT | RDNA 4 (gfx1201) | ROCm 7.2 | Ubuntu 24.04 | Primary AMD test |
| nos2 | NVIDIA GTX 1070 Ti | Pascal (sm_61) | CUDA 11.8 / Driver 580 | Ubuntu 22.04 | CUDA 11.8 mine test |
| nosnode | NVIDIA RTX 2070 | Turing (sm_75) | CUDA 12.6 / Driver 535 | Ubuntu 22.04 | CUDA 12.6 mine test |

### Architecture Coverage Gaps

| Architecture | Status | What's Needed |
|---|---|---|
| Pascal (sm_61) | ✅ Compile + Mine | nos2 (GTX 1070 Ti) |
| Turing (sm_75) | ✅ Compile + Mine | nosnode (RTX 2070) |
| Ampere (sm_80/86) | ✅ Compile only | Need RTX 3060-3090 or A100 |
| Ada (sm_89) | ✅ Compile only | Need RTX 4060-4090 |
| Hopper (sm_90) | ✅ Compile only | Need H100/H200 (cloud) |
| Blackwell (sm_100/120) | ✅ Compile only | Need RTX 5090/B200 (future) |

---

## Running the Matrix

```bash
# Full compile validation (all 10 combos)
./scripts/matrix-test.sh

# Quick smoke test
./scripts/matrix-test.sh --quick

# Filter by keyword
./scripts/matrix-test.sh --filter "12.8"
./scripts/matrix-test.sh --filter "opencl"

# Build artifacts for distribution
./scripts/build-matrix.sh

# Build + hardware mine test
./scripts/build-matrix.sh --test
# OR
./scripts/matrix-test.sh --test
```
