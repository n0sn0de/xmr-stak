# Session Notes — n0s-cngpu Refactor

## How This Works
A cron job fires every 20 minutes. Each session:
1. Read this file for context on where we left off
2. Read REFACTOR-PLAN.md for the task list
3. Pick up the next task, work on it
4. Update this file with progress, blockers, and next steps
5. Commit and push work

## ⚠️ BRANCHING RULES — READ FIRST
- **Phase 1 and Phase 2 are MERGED into master.** Old branches deleted.
- **Always work on master** unless creating a new phase branch.
- **For Phase 3:** Create branch `phase3/test-harness` from current master, do the work, merge back to master when complete, then delete the branch.
- **Never commit build artifacts** (build-test/, *.o, *.a, binaries). Check .gitignore first.
- **Keep branches short-lived.** Merge to master as soon as a phase is complete.

---

## Current Task
**Phase 3: Podman Test Harness — NOT STARTED**

## Next Steps
1. Create branch `phase3/test-harness` from master
2. Create `containers/` directory
3. Write Containerfiles for Ubuntu LTS: bionic (18.04), focal (20.04), jammy (22.04), noble (24.04)
4. Each builds CPU-only (`-DOpenCL_ENABLE=OFF -DCUDA_ENABLE=OFF`) and runs smoke test
5. Write `scripts/test-all-distros.sh` orchestrator
6. Start with noble (24.04) as first target — matches our dev machine
7. Test locally with `podman build`, verify pass/fail
8. When all 4 distros pass, merge to master, delete branch

## Blockers
_(none)_

## Completed Phases
- **Phase 1 ✅** — Dev fee removal, algorithm strip to cryptonight_gpu only (merged to master)
- **Phase 2 ✅** — Rebrand to n0s-cngpu, license compliance, config simplification (merged to master)

## Session Log

### Session 9 — 2026-03-28 17:12 CDT (Branch Consolidation)
- Jason requested branch review
- Merged phase1/fee-removal-cleanup → master (no-ff merge)
- Merged phase2/rebrand → master (no-ff merge)
- Cleaned committed build-test/ artifacts, added to .gitignore
- Deleted local+remote branches: phase1/fee-removal-cleanup, phase2/rebrand, phase3/test-harness
- Pushed clean master to origin
- Updated SESSION-NOTES.md with branching rules
- **Next session:** Start Phase 3 from clean master

### Sessions 0-8 — See git log for details
- Session 0: Planning and codebase audit
- Sessions 1-4: Phase 1 (dev fee removal, algorithm cleanup)
- Sessions 5-8: Phase 2 (rebrand, license, config simplification)
