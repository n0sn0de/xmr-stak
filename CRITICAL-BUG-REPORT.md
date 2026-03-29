# CRITICAL: Miner Has Never Worked Since Rebrand

## Discovery
2026-03-28 22:18 CDT — While attempting deep algorithm cleanup, discovered that **even the unmodified v1.0.0 release cannot mine**. The rebrand from xmr-stak to n0s-cngpu broke the miner, and it was never runtime-tested.

## Root Cause #1: Enum Value Mismatch (FIXED)
When the Phase 2 rebrand stripped the `cryptonight.hpp` enum to only `{invalid_algo, cryptonight_gpu}`, the default C++ enum made `cryptonight_gpu = 1`. But the OpenCL code expects `ALGO = 13` (from the original xmr-stak). This caused:
- `CL_INVALID_KERNEL_NAME` when creating kernels (looking for `cn0_cn_gpu1` instead of `cn0_cn_gpu13`)

**Fix applied:** Set `cryptonight_gpu = 13` explicitly in both `cryptonight.hpp` and `cryptonight.cl`

## Root Cause #2: Unknown Kernel Dispatch Issue (UNFIXED)
After fixing the enum, kernels are created successfully but dispatch fails with:
- `CL_INVALID_KERNEL when calling clEnqueueNDRangeKernel for kernel 0`

This suggests:
- Wrong work group sizes
- Wrong kernel argument types
- Buffer mismatches
- Something in the dispatch code path that doesn't match the kernel signature

## Testing Timeline
- v1.0.0 (70a110b): ❌ Broken — `CL_INVALID_KERNEL_NAME`
- c8871ab (pre-cleanup): ❌ Broken — `CL_INVALID_KERNEL_NAME`
- cleanup/algo-careful + enum fix: ❌ Broken — `CL_INVALID_KERNEL`

## Why This Wasn't Caught
1. **No runtime testing during rebrand** — Phases 1-3 only did build verification, never actual mining
2. **Container builds were CPU-only** — No GPU testing in Podman harness
3. **Benchmark mode (`--benchmark`) doesn't use the pool/stratum path** — May have hidden dispatch issues

## Recommended Next Steps

### Option 1: Revert to Original xmr-stak Enum (SAFEST)
Keep ALL the original algorithm enum values from xmr-stak, even for dead algos. Only restrict at runtime (coins[] array). This ensures enum indices match everywhere.

### Option 2: Deep Kernel Dispatch Debug (TIME-CONSUMING)
1. Add extensive logging to `gpu.cpp` around kernel creation and argument setting
2. Verify every `clSetKernelArg` call matches the actual kernel signature
3. Check work group sizes vs. kernel `__attribute__((reqd_work_group_size(...)))`
4. Test with a minimal kernel dispatch (just cn0, no full mining loop)

### Option 3: Compare with Working xmr-stak Binary
1. Build original xmr-stak from upstream `master` with `cryptonight_gpu` support
2. Test that it mines successfully
3. Binary diff the working vs. broken builds to find the delta

## Impact
**CRITICAL** — The miner is completely non-functional. Cannot mine on any pool. All previous "releases" (v1.0.0, etc.) are broken.

## Time Investment So Far
~4 hours of debugging (enum fix, cleanup attempts, reverting, retrying)

## Estimated Time to Fix
- Option 1: 30 minutes
- Option 2: 3-5 hours
- Option 3: 1-2 hours

## Recommendation
**Option 1** is the safest short-term fix. Get mining working FIRST, then attempt cleanup later with proper runtime testing after EVERY commit.
