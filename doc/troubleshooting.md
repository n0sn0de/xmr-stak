# Troubleshooting

## OpenCL Issues

### CL_MEM_OBJECT_ALLOCATION_FAILURE

The GPU can't allocate the requested memory. Solutions:

- Check for too many threads per GPU (`index` values in `amd.txt`)
- Lower `intensity` in `amd.txt` (must be a multiple of `worksize`)
- Add virtual memory / increase swap

### GPU not detected

- Verify your GPU driver is installed: `clinfo` should list your device
- Make sure `ocl-icd-opencl-dev` is installed
- Check that the OpenCL ICD file exists in `/etc/OpenCL/vendors/`

### Invalid Result GPU ID

Common causes:

- **Hardware:** overclock/overvoltage — try stock clocks first
- **Drivers:** try a different driver version (blockchain drivers, or latest stable)
- **Config:** reduce `intensity` or `worksize` in `amd.txt`

## NVIDIA/CUDA Issues

### CUDA not found during build

Ensure CUDA is in your PATH:

```bash
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

### Invalid Result GPU ID (NVIDIA)

- Reduce `threads` or `bfactor` in `nvidia.txt`
- Reduce overclock / try stock clocks
- Update to latest NVIDIA driver

## Build Issues

### Internal compiler error: Killed

Out of memory during compilation. Solutions:

- Add swap: `sudo fallocate -l 2G /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile`
- Reduce parallel jobs: `make -j1`
- At least 1 GB RAM recommended

### Illegal Instruction

The binary was built with CPU-specific optimizations (`-DXMR-STAK_COMPILE=native`) and you're running it on a different CPU. Rebuild with `-DXMR-STAK_COMPILE=generic` for portable binaries.

## Network Issues

### Pool connection refused / timeout

- Verify pool address and port in `pools.txt`
- Check firewall rules: `curl -v pool.ryo-currency.com:3333` (should connect)
- Try a different pool or port

### IP is banned

Your IP was banned by the pool, usually for:

- Too many invalid shares (check GPU stability)
- Too many connection attempts (check config for errors)
- Using an incompatible algorithm

Contact the pool operator or try a different pool.

### TLS connection errors

- Ensure `libssl-dev` was installed at build time
- Verify the pool supports TLS on the configured port
- Try `"use_tls" : false` to rule out TLS issues

## Performance Issues

### Low hashrate

- Run `--benchmark 10` to establish baseline
- Check GPU temperatures — throttling reduces hashrate
- Follow the [Tuning Guide](tuning.md) for optimization
- Ensure no other GPU-intensive applications are running

### Hashrate drops over time

- Monitor GPU temperatures (thermal throttling)
- Check for driver crashes in `dmesg`
- Try lowering intensity/threads slightly for stability
