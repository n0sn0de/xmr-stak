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
**Phase 1, Task 1.1: Remove Developer Fee System**

## Next Steps
1. Create branch `phase1/fee-removal-cleanup` from main
2. Start with `donate-level.hpp` deletion and ripple through all references
3. Remove dev pool infrastructure from executor.cpp and jpsock
4. Remove `pool_coin[2]` dev pool slot from coinDescription.hpp
5. Build and verify compilation succeeds

## Blockers
_(none yet)_

## Session Log

### Session 0 — 2026-03-28 15:49 CDT (Planning)
- Audited full xmr-stak codebase (~40K lines C/C++)
- Created REFACTOR-PLAN.md with 5 phases, ~50 subtasks
- Identified all files touched by dev fee (8 files)
- Identified all non-CNGPU algorithm code for removal
- NVIDIA backend: 23 files, entire directory to delete
- Set up cron job for 20-minute iteration cycles
- **Next session:** Start Phase 1.1 — remove developer fee
