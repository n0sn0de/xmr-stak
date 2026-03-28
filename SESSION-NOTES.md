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
**Phase 1, Task 1.2: Remove Non-CNGPU Algorithm Code** (IN PROGRESS - NEW BLOCKER)

## Next Steps
**NEW BLOCKER:** cryptonight_aesni.h has deep CryptonightR integration with template-based
v4_random_math calls that cannot be easily stubbed. Simple stubs don't match template signatures.

**Two paths forward:**
1. **#ifdef approach** - Wrap all CryptonightR code paths in cryptonight_aesni.h with compile-time guards
2. **Skip CPU cleanup** - Keep algorithm enum and CPU code as-is, focus only on removing unused OpenCL/CUDA kernels

**Recommendation:** Option 2 (skip CPU cleanup for now). The CPU backend is not the primary target
for n0s-cngpu. Focus on:
1. Remove unused OpenCL kernels (keep only cryptonight_gpu.cl)
2. Remove unused CUDA kernels (keep only cn_gpu variants)
3. Continue to Phase 2 (rebrand)
4. Mark CPU backend for Phase 1.5 optional cleanup

**Next session action items:**
1. Revert the WIP commit that breaks compilation
2. Accept that coins[] array strip (commit fb5e124) is sufficient for runtime restrictions
3. Move to OpenCL kernel cleanup (remove cryptonight.cl, keep only cryptonight_gpu.cl)
4. Move to Phase 2 if kernel cleanup succeeds

## Blockers
_(none yet)_

## Session Log

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
