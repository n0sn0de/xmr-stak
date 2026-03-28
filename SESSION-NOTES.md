# Session Notes — n0s-cngpu Refactor

## How This Works
A cron job fires every 20 minutes. Each session:
1. Read this file for context on where we left off
2. Read REFACTOR-PLAN.md for the task list
3. Pick up the next task, work on it
4. Update this file with progress, blockers, and next steps
5. Commit and push work

---

## Current Task
**Phase 2, Task 2.1: Project Identity (IN PROGRESS)**

## Next Steps
**Phase 2.1 partial complete:** Basic rebrand done, more user-facing strings to update
1. ✅ CMake project name updated
2. ✅ Binary output renamed to n0s-cngpu
3. ✅ Version strings updated (1.0.0)
4. ✅ Startup banner updated
5. ✅ Build verified working

**Remaining for Task 2.1:**
- Update HTTP dashboard title/headers
- Update error messages with xmr-stak references
- Search for any remaining "xmr-stak" strings in user-facing output

**Next subtasks (Phase 2):**
- Task 2.2: License compliance (copyright headers, NOTICE file)
- Task 2.3: Configuration simplification (remove coin selection)
- Task 2.4: Clean up legacy branding in configs

**Next session action:** Continue Task 2.1 — update HTTP dashboard and remaining user-facing strings

## Blockers
_(none yet)_

## Session Log

### Session 5 — 2026-03-28 16:54 CDT (Phase 2 Rebrand Started)
✅ **Completed:**
- **Phase 2 Task 2.1 (partial): Project Identity**
  * CMake project renamed: `xmr-stak` → `n0s-cngpu`
  * Binary output renamed: `bin/xmr-stak` → `bin/n0s-cngpu`
  * Version updated: `2.10.8` → `1.0.0`
  * Product name macro: `XMR_STAK_NAME` → `"n0s-cngpu"`
  * Startup banner rebranded:
    - Added n0s-cngpu identity
    - Simplified RYO Currency branding
    - Retained original author credits (GPLv3 compliance)
  * Build verified working
  * Version output verified: `n0s-cngpu 1.0.0 [git-hash]`
  * Help output verified: Usage shows `n0s-cngpu`

**Files modified:**
- CMakeLists.txt: project name, executable name, install target
- xmrstak/version.cpp: XMR_STAK_NAME, XMR_STAK_VERSION
- xmrstak/cli/cli-miner.cpp: startup banner, help text

**Branch:** `phase2/rebrand` (pushed to origin)
**Commit:** ea2c2a9 "Phase 2 Task 2.1: Rebrand to n0s-cngpu"

**Remaining work for Task 2.1:**
- HTTP dashboard title/branding
- Remaining user-facing strings (error messages, prompts)

**Next session:** Continue Task 2.1 (HTTP dashboard + remaining strings), then Task 2.2 (license compliance)

### Session 4 — 2026-03-28 16:34 CDT (Phase 1 Complete, Phase 2 Prep)
✅ **Completed:**
- Reverted WIP commit 0ac153f that broke the build
- Build verified working after revert (commit 228a48a)
- Assessed OpenCL kernel complexity: cryptonight.cl is base template, cryptonight_gpu.cl injected via regex
- **Decision:** Accept runtime restriction (coins[] strip) as sufficient for Phase 1
- Marked deep algorithm cleanup as optional Phase 1.5 for future
- Updated refactor plan: Phase 1 → Phase 2 next session

**Analysis:**
- OpenCL kernel architecture uses cryptonight.cl as base, injects cryptonight_gpu via XMRSTAK_INCLUDE_CN_GPU regex
- Not as modular as initially thought — cryptonight_gpu.cl depends on cryptonight.cl structure
- CPU backend has 1000+ lines of template-heavy algorithm code (cryptonight_aesni.h, etc.)
- Risk/reward assessment: runtime restriction via coins[] achieves the goal with minimal risk

**Commit log:**
- 228a48a: "Revert 'WIP: Remove CryptonightR files and ASM optimizations'" — ✅ WORKING BUILD
- fb5e124: "Strip coins array to cryptonight_gpu only (stub approach)" — ✅ RUNTIME RESTRICTION COMPLETE

**Next session:** Phase 2 Task 2.1 — Project Identity (rename to n0s-cngpu, update version strings)

### Session 3 — 2026-03-28 16:19 CDT (CryptonightR File Removal - BLOCKED)
✅ **Completed:**
- Adopted stub approach: kept algorithm enum intact, stripped coins[] array to cryptonight_gpu + ryo only
- Build succeeds with stub approach (commit fb5e124)
- Removed CryptonightR files:
  - CryptonightR_gen.cpp/.hpp (CPU/AMD/NVIDIA)
  - CryptonightR_template ASM files + WOW templates
  - variant4_random_math.h
- Removed cryptonight_v8_main_loop ASM files (not needed for cn_gpu)
- Removed xmr-stak-asm CMake target entirely
- Removed all xmr-stak-asm link dependencies from CMakeLists.txt

⚠️ **NEW BLOCKER:** cryptonight_aesni.h has template-based CryptonightR code that cannot compile
with simple stubs. Template parameter mismatch on v4_random_math_init<ALGO>() calls.

**Commit log:**
- fb5e124: "Strip coins array to cryptonight_gpu only (stub approach)" — ✅ WORKING BUILD
- 0ac153f: "WIP: Remove CryptonightR files and ASM optimizations" — ❌ BREAKS BUILD

**Decision:** Revert WIP commit, accept coins[] strip as sufficient. Skip deep CPU backend cleanup.
Focus on OpenCL/CUDA kernel cleanup instead.

**Next session:** Revert 0ac153f, move to OpenCL kernel cleanup.

### Session 2 — 2026-03-28 16:13 CDT (Algorithm Enum Strip - BLOCKED)
⚠️ **Blocked on CPU backend complexity**
- Stripped algorithm enum down to `invalid_algo` + `cryptonight_gpu` (commit ed72d64)
- Simplified coins[] array to only cryptonight_gpu and ryo
- **Build fails:** CPU backend has 1000+ lines of algorithm-specific code in cryptonight_aesni.h
  that references removed algorithm IDs in templates and switch statements
- **Decision:** Revert aggressive stripping, adopt stub approach instead
- **Commit:** ed72d64 "Strip algorithm enum and coins array to cryptonight_gpu only" (will revert)
- **Next session:** Revert this commit, keep enum as stubs, remove from coins[] only

### Session 1 — 2026-03-28 16:00 CDT (Dev Fee Removal)
✅ **Task 1.1 Complete: Developer Fee System Removed**
- Deleted `xmrstak/donate-level.hpp`
- Removed all `fDevDonationLevel` references (version.hpp, executor.cpp)
- Removed `is_dev_pool()` method and `pool` member from jpsock
- Removed dev pool infrastructure from executor.cpp:
  - Removed `is_dev_time()` function
  - Removed dev pool switching logic
  - Removed dev pool URL injection (donate.xmr-stak.net)
  - Simplified `get_live_pools()` to only handle user pools
  - Removed all `is_dev_pool()` conditional checks
- Removed donation message from cli-miner.cpp
- Removed dev pool checks from socket.cpp TLS validation
- Updated executor.hpp to remove dev donation period constants
- **Build verified:** CPU-only build succeeds
- **Commit:** 5bae325 "Remove developer fee system"
- **Branch pushed:** origin/phase1/fee-removal-cleanup
- **Files modified:** 8 files, -143 lines of code
- **Next session:** Task 1.2 — strip non-CNGPU algorithms

### Session 0 — 2026-03-28 15:49 CDT (Planning)
- Audited full xmr-stak codebase (~40K lines C/C++)
- Created REFACTOR-PLAN.md with 5 phases, ~50 subtasks
- Identified all files touched by dev fee (8 files)
- Identified all non-CNGPU algorithm code for removal
- NVIDIA backend: kept (CNGPU supports both AMD and NVIDIA)
- Set up cron job for 20-minute iteration cycles
