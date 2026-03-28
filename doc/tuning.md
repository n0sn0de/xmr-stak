# n0s-cngpu Tuning Guide

## Benchmarking

Two ways to benchmark:

1. **Config-based:** Set `verbose_level` to `4` and `h_print_time` to `30` in `config.txt`, then start the miner. Hashrate reports appear every 30 seconds.

2. **CLI benchmark:** `./n0s-cngpu --benchmark 10` — runs a 60-second offline benchmark with all enabled backends (no pool needed).

## GPU Management

Each GPU backend has its own config file (`amd.txt` or `nvidia.txt`) with a `GPU_threads_conf` section. Add or remove entries to enable/disable specific GPUs. The `index` refers to the GPU device number.

---

## NVIDIA Backend

Configuration file: `nvidia.txt`

### Threads and Blocks

The key parameters are `threads` (threads per block) and `blocks` (CUDA blocks to launch).

- `blocks` should be a multiple of your GPU's multiprocessor count `M`
- For GPUs with compute capability < 6.0: `threads × blocks × 2 ≤ 1900` (MB)
- Pascal and newer GPUs support up to 16 GB

Example for a GPU with 24 multiprocessors:
```json
{ "index" : 0, "threads" : 16, "blocks" : 48, "bfactor" : 0, "bsleep" : 0,
  "affine_to_cpu" : false, "sync_mode" : 3, "mem_mode" : 1 }
```

### Multiple GPUs

Add one entry per GPU in `GPU_threads_conf`:

```json
"GPU_threads_conf" :
[
    { "index" : 0, "threads" : 16, "blocks" : 48, ... },
    { "index" : 1, "threads" : 16, "blocks" : 48, ... }
]
```

---

## AMD Backend

Configuration file: `amd.txt`

### Intensity and Worksize

- **intensity** — number of threads used. Max = `GPU_MEMORY_MB / 2 - 128`, but the optimum is often lower for 4GB+ cards.
- **worksize** — threads working together. Usually `8` or `16` is optimal.

### Two Threads per GPU

Some AMD GPUs perform better with two threads per GPU. Duplicate the index entry:

```json
"GPU_threads_conf" :
[
    { "index" : 0, "intensity" : 1000, "worksize" : 8, "affine_to_cpu" : false,
      "strided_index" : true, "mem_chunk" : 2, "unroll" : 8, "comp_mode" : true,
      "interleave" : 40 },
    { "index" : 0, "intensity" : 1000, "worksize" : 8, "affine_to_cpu" : false,
      "strided_index" : true, "mem_chunk" : 2, "unroll" : 8, "comp_mode" : true,
      "interleave" : 40 }
]
```

Note: Memory usage doubles — reduce `intensity` accordingly.

### Interleave Tuning

`interleave` controls timing when two threads share a GPU. Default `40` works for most cases.

Optimal: `last delay` in logs settles to 10–15 and messages become rare.
Not optimal: `last delay` jumps or climbs — try adjusting `interleave` by ±2–5, lowering `intensity`, or reducing GPU overclock.

### Auto-Tune

Set `"auto_tune" : 6` in `amd.txt` (6–10 rounds per intensity check). The miner tests intensity values and reports the best:

```
OpenCL 0|0: lock intensity at 896
```

Write down the locked values, set `auto_tune` back to `0`, and enter the optimal intensity.

### Compatibility Mode

`comp_mode` enables extra checks for GPU compatibility. When `false`, `intensity` must be a multiple of `worksize`.

### Memory Pool (Linux)

Set these environment variables before starting to allow larger allocations:

```bash
export GPU_FORCE_64BIT_PTR=1
export GPU_MAX_HEAP_SIZE=100
export GPU_MAX_ALLOC_PERCENT=100
export GPU_SINGLE_ALLOC_PERCENT=100
```

### Scratchpad Indexing

Try changing `strided_index` from `true` to `false` or `2` in `amd.txt`. When set to `2`, fine-tune with `mem_chunk`.

---

## CPU Backend

Configuration file: `cpu.txt`

### Low Power Mode

`low_power_mode` ranges from `1` to `5`. Value `N` increases single-thread performance by ~N× but requires `2×N` MB of cache per thread.

- `false` or `1` — normal (default)
- `true` or `2` — 2× performance, 4 MB cache/thread
- `5` — 5× performance, 10 MB cache/thread (for CPUs with large L3/L4 cache)

---

## General Tips

- Start with auto-detected defaults, then tune one parameter at a time
- Monitor temperatures — mining is a sustained thermal load
- Use `--benchmark 10` to test changes without pool connections
- Check `verbose_level` 4 output for per-device hashrates
