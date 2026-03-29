# n0s-ryo-miner Refactor Plan v2

**Goal:** Transform xmr-stak into a lean, focused CryptoNight-GPU miner for RYO Currency supporting AMD (OpenCL) and NVIDIA (CUDA) GPUs only.

**Guiding Principles:**
1. **Test after EVERY change** — `./test-both.sh` must pass before committing
2. **One change at a time** — no batching, no "while we're at it"
3. **Respect the archaeology** — this code has battle scars for a reason
4. **Enum indices are sacred** — they're ABI contracts between C++, OpenCL, and CUDA

---

## Phase 1: Moderate Cleanup (Foundation) — ✅ COMPLETE

**Objective:** Rebrand, remove CPU backend, clean up configs — but keep algorithm enum intact.

**Status:**
- ✅ 1.1: Project Rename (binary + banners)
- ✅ 1.2: Remove CPU Mining Backend
- ✅ 1.3: HTTP Daemon Decision (keep it)
- ✅ 1.4: Hardcode Algorithm to CryptoNight-GPU
- ✅ 1.5: Keep Algorithm Enum Intact (no changes needed)

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

### 1.2: Remove CPU Mining Backend ✅ COMPLETE

> **⚠️ CORRECTION (discovered during implementation):**
> The original plan stated "CPU backend is completely isolated in `xmrstak/backend/cpu/`. GPU backends don't depend on it." **This is wrong.**
>
> Both GPU backends (AMD & NVIDIA) heavily depend on `cpu/crypto/` and `cpu/minethd.*`:
> - `cryptonight.h`, `cryptonight_aesni.h` — hash verification
> - `minethd::minethd_alloc_ctx()` — crypto context allocation
> - `minethd::thd_setaffinity()` — thread pinning
> - `minethd::func_multi_selector()` — hash function dispatch
> - `hwlocMemory.hpp` — NUMA-aware memory allocation
> - `variant4_random_math.h` — CryptonightR math (used by GPU CryptonightR codegen)
>
> **Resolution:** Only the CPU *mining threads* were removed, not the shared crypto library.
> The `xmrstak/backend/cpu/` directory stays as shared infrastructure for GPU backends.

#### What was actually done:
- Hardcoded `CONF_NO_CPU` in CMake (removed `CPU_ENABLE` option entirely)
- Removed `--noCPU` and `--cpu FILE` CLI options
- Removed `useCPU` param from `params.hpp`
- Removed CPU thread_starter block from `backendConnector.cpp`
- Updated test scripts (removed `--noCPU` flag, added branch-aware remote deploy)

**Test results:** AMD 104 shares ✅ | NVIDIA 44 shares ✅

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

### 1.4: Hardcode Algorithm to CryptoNight-GPU ✅ COMPLETE

> **Discovery:** Steps 1.4.1–1.4.3 were combined into a single commit because they're
> tightly coupled — the coins array IS the pool config, and the CLI feeds into it.
> Splitting them would leave broken intermediate states. One atomic change was the right call.

#### What was actually done:
- Removed `--currency` from help text; kept CLI flag as deprecated (backward-compat, prints warning)
- Hardcoded `params::inst().currency = "cryptonight_gpu"` as default
- Removed interactive currency selection prompt from guided config
- Reduced `coins[]` array in `jconf.cpp` from 30+ entries to just 2: `cryptonight_gpu` and `ryo`
- Simplified `pools.tpl` template (removed massive dead algorithm documentation)
- Removed `--currency cryptonight_gpu` from test scripts (now default, not needed)

> **Correction to original plan:**
> - 1.4.2 said to modify `jpsock.cpp` for pool switching — **not needed**. The pool switching
>   logic in `jpsock.cpp` works via `coin_selection` struct which now only has cn_gpu entries.
>   No code changes needed in the network layer.
> - 1.4.3 said coins array was in `cryptonight.hpp` — **it's actually in `jconf.cpp`**.
>   The `cryptonight.hpp` file has the algorithm enum, not the coins array.

**Test results:** AMD 78 shares ✅ | NVIDIA 51 shares ✅

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

## Phase 2: Aggressive Cleanup — ✅ COMPLETE

**Objective:** Strip dead algorithm code from OpenCL/CUDA kernels.

### 2.1: Strip Dead Kernel Code (One File at a Time) — ✅ COMPLETE

**Results:**
1. ✅ `cryptonight.cl` (OpenCL shared kernels) — 1448 → 1167 lines (-19%)
   - Stripped: heavy, v8, reversewaltz, conceal, bittube2, monero, aeon, ipbc, stellite, masari
   - Made cn_gpu code paths unconditional
   - Kept Blake/JH/Skein/Groestl (host still creates kernel objects)
   - AMD: 88 shares ✅

2. ✅ `cuda_core.cu` (CUDA main kernels) — 1089 → 263 lines (-76%)
   - Removed: phase1, phase2_double, phase2_quad, cryptonight_core_gpu_hash
   - Removed: CryptonightR codegen, func_table, dead helper code (cuda_mul128, shuffle64, u64)
   - Removed: unused includes (CudaCryptonightR_gen, fast_div_heavy, fast_int_math_v2)
   - Simplified phase3 and _hash_gpu: made cn_gpu conditionals unconditional
   - Replaced func_table dispatch with direct cryptonight_gpu call
   - NVIDIA: 60 shares ✅

3. ✅ `cuda_extra.cu` (CUDA helper kernels) — 820 → 585 lines (-29%)
   - Simplified prepare: removed heavy/v8/CryptonightR branches
   - Simplified final: removed branch dispatcher, kept cn_gpu 16-round AES + mix_and_propagate
   - Simplified init: removed heavy/v8/CryptonightR memory extensions
   - Replaced dispatchers with direct cryptonight_gpu template calls
   - NVIDIA: 53 shares ✅ (after fixing missing mix_and_propagate loop)

**Test results (final integration):**
- AMD RX 9070 XT: 85 shares ✅
- NVIDIA GTX 1070 Ti: 53 shares ✅
- Both GPUs verified together

**Code reduction:**
- OpenCL: 1448 → 1167 lines (-19%)
- CUDA core: 1089 → 263 lines (-76%)
- CUDA extra: 820 → 585 lines (-29%)
- **Total kernel code: 3357 → 2015 lines (-40%)**

### 2.2: Clean Up Algorithm Enum — ✅ COMPLETE

**Results:**
- ✅ Reduced `xmrstak_algo_id` from 22 entries → 2: `invalid_algo = 0`, `cryptonight_gpu = 13`
- ✅ Explicit `= 13` preserved for ABI contract (OpenCL `#define`, CUDA `-DALGO=`)
- ✅ Simplified `POW()` from 18-element array lookup to direct return
- ✅ Simplified `get_algo_name()` from dual-array lookup to simple switch
- ✅ Removed dead algo branches from: `gpu.cpp`, `backendConnector.cpp`, `minethd.cpp`
- ✅ Replaced dead algo template references with `invalid_algo` in `cryptonight_aesni.h`
- ✅ Simplified `func_multi_selector` from 16-entry switch+64-entry function table to 4-entry GPU-only table
- ✅ Removed CryptonightR switch/case duplicates in OpenCL/CUDA codegen
- ✅ Added explicit template instantiations for `.so` plugin symbol resolution
- ✅ Kept `CN_ITER`/`CN_MASK` constants for ASM patching code compatibility

**Code reduction:** 9 files changed, -658 lines net

**Test results:**
- AMD RX 9070 XT: 150+ shares accepted, 0 rejected ✅
- NVIDIA GTX 1070 Ti: 90+ shares accepted, 0 rejected ✅

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
| Remove CPU backend | Medium | ~~Backend is isolated~~ CPU crypto is shared! Only removed CPU mining threads |
| Remove CLI options | ✅ Done | `--currency` deprecated with backward-compat |
| Hardcode algorithm | ✅ Done | coins[] reduced, default hardcoded, tests pass |
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
- ✅ Binary named `n0s-ryo-miner` (Phase 1.1.1)
- ✅ Banners show "n0s-ryo-miner" and "RYO Currency" (Phase 1.1.3)
- ✅ CPU mining threads removed (Phase 1.2 — note: crypto lib stays as shared infra)
- ✅ HTTP daemon kept as-is (Phase 1.3)
- ✅ Algorithm hardcoded to cn_gpu, `--currency` deprecated (Phase 1.4)
- ✅ `./test-both.sh` passes consistently (AMD + NVIDIA verified each phase)
- ⬜ No dead code warnings in build (minor — some warnings remain)
- ⬜ README updated with new name/usage

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
