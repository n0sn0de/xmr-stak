# n0s-ryo-miner Refactor Plan v2

**Goal:** Transform xmr-stak into a lean, focused CryptoNight-GPU miner for RYO Currency supporting AMD (OpenCL) and NVIDIA (CUDA) GPUs only.

**Guiding Principles:**
1. **Test after EVERY change** — `./test-both.sh` must pass before committing
2. **One change at a time** — no batching, no "while we're at it"
3. **Respect the archaeology** — this code has battle scars for a reason
4. **Enum indices are sacred** — they're ABI contracts between C++, OpenCL, and CUDA

---

## Phase 1: Moderate Cleanup (Foundation)

**Objective:** Rebrand, remove CPU backend, clean up configs — but keep algorithm enum intact.

### 1.1: Project Rename (Test After Each File)

**Why separate commits:** Each file type has different blast radius. Binary names affect scripts, source names affect includes, banners affect user experience.

#### 1.1.1: Binary & Build Artifacts
- `CMakeLists.txt`: `project(xmr-stak)` → `project(n0s-ryo-miner)`
- Binary output: `xmr-stak` → `n0s-ryo-miner`
- Shared libraries: `libxmrstak_*` → `libn0s_*` (or keep for now?)
- Test: Verify build produces `n0s-ryo-miner` binary
- **Risk:** Low — affects only build output

#### 1.1.2: Source File Names (Defer?)
- **Decision:** Keep `xmrstak/` directory structure for now
- **Reason:** Massive refactor, affects every `#include`, may break builds
- **Future:** Phase 2 or 3, once everything else is stable

#### 1.1.3: Banners & Version Strings
- `xmrstak/version.xmrstak.cmake`: version strings
- `xmrstak/cli/cli-miner.cpp`: startup banner
- `xmrstak/version.cpp`: version reporting
- Test: Check startup banner shows "n0s-ryo-miner"
- **Risk:** Low — cosmetic only

#### 1.1.4: Config File Names
- `pools.txt`, `cpu.txt`, `amd.txt`, `nvidia.txt` → keep or rename?
- **Decision:** Keep for compatibility, document in README
- **Reason:** Users may have existing configs
- **Risk:** None — just documentation

**Test milestone:** `./test-both.sh` with rebranded binary

---

### 1.2: Remove CPU Backend

**Why this is safe:** CPU backend is completely isolated in `xmrstak/backend/cpu/`. GPU backends don't depend on it.

#### 1.2.1: Delete CPU Source Tree
```bash
rm -rf xmrstak/backend/cpu/
```
- Includes: `minethd.cpp`, `crypto/`, all CPU mining code
- Test: Verify build succeeds with `-DCPU_ENABLE=OFF` (already default)
- **Risk:** Low — backend is isolated

#### 1.2.2: Remove CPU CMake Logic
- `xmrstak/backend/CMakeLists.txt`: remove CPU backend options
- `CMakeLists.txt`: remove `-DCPU_ENABLE` flag entirely
- Test: Clean build, check no CPU references remain
- **Risk:** Low — cmake is declarative

#### 1.2.3: Remove CPU Config & CLI Options
- `xmrstak/backend/cpu/jconf.*`: delete
- `xmrstak/cli/cli-miner.cpp`: remove `--noCPU`, `--cpu FILE` options
- `xmrstak/params.hpp`: remove CPU-related params
- Test: `./n0s-ryo-miner --help` shows no CPU options
- **Risk:** Medium — affects CLI parsing

#### 1.2.4: Remove CPU from Executor
- `xmrstak/backend/globalStates.cpp`: remove CPU thread management
- `xmrstak/backend/executor.*`: remove CPU backend init
- Test: `./test-both.sh` (GPU-only operation)
- **Risk:** Medium — affects runtime initialization

**Test milestone:** `./test-both.sh` with CPU backend completely removed

---

### 1.3: HTTP Daemon Decision

**Keep it.** Here's why:
- **Rationale:** The HTTP daemon provides a JSON API and web UI. Useful for:
  - Remote monitoring (pool stats, hashrate, temps)
  - Potential future GUI (wrap this CLI as a backend service)
  - Debugging (live stats without console)
- **Cost:** ~5 source files, libmicrohttpd dependency
- **Benefit:** Ready-made API if we ever build a GUI or dashboard
- **Action:** Keep, but make optional in CMake (already is with `-DMICROHTTPD_ENABLE`)

**No changes needed** — just document in README that HTTP API is available.

---

### 1.4: Hardcode Algorithm to CryptoNight-GPU

**Why this is safe:** Runtime restriction only, keeps enum intact for OpenCL/CUDA compatibility.

#### 1.4.1: Remove Algorithm Selection UI
- `xmrstak/cli/cli-miner.cpp`: remove `--currency` option
- Hardcode: `POW(cryptonight_gpu)` always
- Test: Binary still runs, no need to specify `--currency`
- **Risk:** Low — UI only

#### 1.4.2: Simplify Pool Config
- `xmrstak/net/jpsock.cpp`: remove multi-algo pool switching logic
- Single algo: always cn_gpu, no runtime selection
- Test: Pool connection works
- **Risk:** Medium — affects network layer

#### 1.4.3: Remove Coins Array
- `xmrstak/backend/cryptonight.hpp`: `coins[]` array → delete or stub
- Keep `POW(cryptonight_gpu)` function
- Test: `./test-both.sh`
- **Risk:** Low — already runtime-restricted

**Test milestone:** `./test-both.sh` with hardcoded cn_gpu

---

### 1.5: Keep Algorithm Enum Intact (For Now)

**CRITICAL DECISION:** Do NOT touch the enum yet.

```cpp
// KEEP THIS EXACTLY AS-IS for Phase 1
enum xmrstak_algo_id {
    invalid_algo = 0,
    cryptonight = 1,
    cryptonight_lite = 2,
    // ... all 17 entries ...
    cryptonight_gpu = 13,
    // ...
};
```

**Why:**
- OpenCL code has `#define cryptonight_gpu 13` matching this
- CUDA code uses `ALGO=13` from cmake `-DALGO=`
- Changing enum breaks the ABI contract → runtime crashes
- We learned this the hard way (5+ hours of debugging)

**Phase 2 will address this** — but only after everything else is stable.

---

## Phase 2: Aggressive Cleanup (Deferred)

**Objective:** Strip dead algorithm code from OpenCL/CUDA kernels.

**Prerequisites:**
- Phase 1 complete
- All tests passing for 24+ hours
- Fresh eyes after a break

### 2.1: Strip Dead Kernel Code (One File at a Time)

**Order of operations:**
1. Strip `cryptonight.cl` (OpenCL shared kernels)
2. Strip `cuda_core.cu` (CUDA main kernels)  
3. Strip `cuda_extra.cu` (CUDA helper kernels)
4. Test after EACH file

**Method:**
- Remove `#if (ALGO == dead_algo)` blocks
- Keep all `#if (ALGO == cryptonight_gpu)` blocks
- Simplify `#if (... || cryptonight_gpu)` → unconditional if cn_gpu is the only survivor

### 2.2: Clean Up Enum (Final Step)

**Only after all kernel code is stripped:**
- Reduce enum to `{invalid_algo = 0, cryptonight_gpu = 13}`
- Keep `= 13` explicit to maintain ABI
- Update OpenCL `#define cryptonight_gpu 13`
- Verify CUDA `-DALGO=13` still matches

---

## Testing Protocol

### Before Every Commit:
```bash
./test-both.sh
```

### Before Every Push:
```bash
# Clean builds on both architectures
rm -rf build
./test-both.sh

# Check for regressions
git diff HEAD~1 --stat  # Review what actually changed
```

### Red Flags (Abort & Investigate):
- `CL_INVALID_KERNEL` errors
- `CUDA ERROR` messages  
- Hashrate drops >5%
- Share rejection rate >1%
- Segfaults or memory errors

---

## Risk Assessment

| Change | Risk | Mitigation |
|--------|------|------------|
| Binary rename | Low | Test build output |
| Remove CPU backend | Low | Backend is isolated |
| Remove CLI options | Medium | Test `--help`, verify GPU flags work |
| Hardcode algorithm | Medium | Test pool connection |
| HTTP daemon (keep) | None | No changes |
| Enum intact (Phase 1) | None | Explicitly preserving |
| Strip kernel code (Phase 2) | **HIGH** | One file at a time, test after each |
| Clean enum (Phase 2) | **HIGH** | Only after kernels stripped, test extensively |

---

## Rollback Plan

**If anything breaks:**
```bash
git log --oneline -10  # Find last working commit
git reset --hard <commit>
git push origin master --force
```

**Then:**
1. Document what broke in an issue
2. Meditate on why it broke
3. Try a different approach
4. Test MORE before committing

---

## Success Criteria

**Phase 1 Complete When:**
- ✅ Binary named `n0s-ryo-miner`
- ✅ Banners show "n0s-ryo-miner" and "RYO Currency"
- ✅ CPU backend completely removed
- ✅ Algorithm hardcoded to cn_gpu (no `--currency` flag needed)
- ✅ `./test-both.sh` passes consistently
- ✅ No dead code warnings in build
- ✅ README updated with new name/usage

**Phase 2 Complete When:**
- ✅ OpenCL/CUDA kernels stripped to cn_gpu only
- ✅ Enum reduced to 2 entries (invalid + cn_gpu)
- ✅ `./test-both.sh` still passes
- ✅ Hashrate unchanged from baseline
- ✅ Share acceptance rate unchanged
- ✅ Code size reduced by ~40%

---

## Timeline

**Phase 1 (Moderate):** 4-8 hours of focused work
- 1.1: Rename (1-2 hours)
- 1.2: Remove CPU (1-2 hours)
- 1.3: HTTP decision (0 hours, keeping it)
- 1.4: Hardcode algo (1-2 hours)
- 1.5: Document enum preservation (0.5 hours)

**Phase 2 (Aggressive):** 6-12 hours of surgical work
- Deferred until Phase 1 proves stable

**Total:** 10-20 hours spread over days, not one marathon session.

---

## Lessons Embedded

1. **Build-only testing is worthless** — GPU code fails at runtime
2. **Enum indices are ABI** — changing them breaks the OpenCL/CUDA contract
3. **One commit = one change** — batching changes makes debugging impossible
4. **Test before commit** — not after, not "we'll test it later"
5. **Respect the artifact** — this code has 6 years of battle scars

---

*This plan respects the complexity. We're not demolishing a house — we're performing surgery on a living organism.*
