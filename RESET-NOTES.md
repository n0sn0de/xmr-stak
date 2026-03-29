# Reset to Working Baseline — 2026-03-28 22:30 CDT

## What Happened

After 4+ hours of debugging, discovered the miner has been **broken since the Phase 2 rebrand**. The enum value mismatch (cryptonight_gpu = 1 vs 13) was introduced when stripping the enum, and the codebase was never runtime-tested — only build-tested.

## Decision

**Hard reset to commit 5bae325** — "Remove developer fee system"
- This is the LAST known-working commit
- Verified mining: ✅ 1 share accepted in 40s test
- Reset done at 22:25 CDT after attempting quick fixes failed

## What Was Lost

All work from commits 5bae325...c8871ab:
- Phase 1 partial (algorithm cleanup)
- Phase 2 complete (rebrand to n0s-cngpu)
- Phase 3 complete (Podman test harness)
- Deep cleanup attempts (all broken)

Total: ~100+ commits, ~2 weeks of cron agent work

## What We Gained

1. **Test harness** (`test-mine.sh`) — runtime verification before EVERY commit
2. **Knowledge** — enum values MUST match between C++ and OpenCL
3. **Process** — build-only testing is insufficient for GPU mining software

## Next Steps

1. ✅ Baseline verified working (5bae325 + test harness)
2. Apply changes ONE AT A TIME with mining test after each:
   - Keep fee removal ✅ (already in baseline)
   - Rebrand (carefully, test after each file)
   - Algorithm cleanup (test after each section)
   - Container work (optional, after mining confirmed)

## Testing Protocol

**MANDATORY before every commit:**
```bash
./test-mine.sh
```

If test fails:
- Revert the change
- Debug
- Fix
- Test again
- Only commit when test passes

## Time Investment

- Original refactor: ~2 weeks
- Debug + reset: 4+ hours
- New timeline: TBD, but WORKING at each step

## Lesson

**Runtime testing is not optional for GPU software.** Build success means nothing if kernels can't dispatch.
