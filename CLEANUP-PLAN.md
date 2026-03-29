# Careful Deep Cleanup Plan

**Goal:** Strip dead algorithm code from cryptonight.cl without breaking runtime.

**Method:** ONE change at a time, build + mine test after EVERY commit.

## Test Command
```bash
cd ~/xmr-stak && rm -rf build && mkdir build && cd build && \
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON -DCMAKE_BUILD_TYPE=Release \
  -DOpenCL_INCLUDE_DIR=/opt/rocm-7.2.0/include \
  -DOpenCL_LIBRARY=/usr/lib/x86_64-linux-gnu/libOpenCL.so && \
cmake --build . -j$(nproc) && \
timeout 30 ./bin/n0s-cngpu --noCPU --noAMDCache \
  -o 192.168.50.186:3333 \
  -u YOUR_WALLET \
  -p x \
  --currency cryptonight_gpu 2>&1 | tee /tmp/mine-test.log
```

Check for:
- ✅ "HASHRATE REPORT" with >0 H/s
- ❌ "CL_INVALID_*" errors
- ❌ "Error when calling clEnqueue*"

## Steps

### Step 1: Remove dead algorithm defines (keep only invalid_algo + cryptonight_gpu)
- Edit cryptonight.cl lines 17-33
- Build + mine test
- Commit

### Step 2: Add _mm_* float helpers before XMRSTAK_INCLUDE_CN_GPU
- These are used by cryptonight_gpu.cl
- Build + mine test
- Commit

### Step 3: Remove cn1 kernel entirely (~290 lines)
- Delete lines ~591-877 in cryptonight.cl
- cn_gpu uses its own cn1_cn_gpu kernel from cryptonight_gpu.cl
- Build + mine test
- Commit

### Step 4: Remove heavy-only code from cn0 kernel
- Delete the mix_and_propagate + heavy scratchpad init block
- Build + mine test
- Commit

### Step 5: Simplify cn2 kernel
- Make all `#if (ALGO == cryptonight_gpu || ...)` unconditional
- Remove `#else` branches for non-gpu algos
- Keep output buffer write, remove BranchBuf dispatch
- Build + mine test
- Commit

### Step 6: Remove unused .cl file includes
- Check which files cryptonight.cl and cryptonight_gpu.cl actually use
- Remove dead XMRSTAK_INCLUDE_* lines
- Build + mine test
- Commit

### Step 7: Delete unused .cl files
- Only after verifying they're not included anywhere
- Build + mine test
- Commit

### Step 8: Clean up gpu.cpp
- Remove dead buffer allocations
- Remove dead kernel names
- Update kernel indices
- Build + mine test
- Commit

## Current Step: 1
