# FAQ

## What does n0s-cngpu mine?

n0s-cngpu mines [RYO Currency](https://ryo-currency.com) using the CryptoNight-GPU algorithm. It does not support other coins or algorithms.

## Is there a developer fee?

No. 100% of your hashrate goes to your configured pool. Zero fees.

## Can I mine other coins?

No. n0s-cngpu is a single-algorithm miner for CryptoNight-GPU only. If you need multi-algorithm support, look at the upstream [xmr-stak](https://github.com/fireice-uk/xmr-stak) project.

## What GPUs are supported?

- **AMD** — any GPU supported by the OpenCL driver (AMDGPU-PRO or ROCm)
- **NVIDIA** — any GPU supported by CUDA 8.0+ (compute capability 2.0+)

## Can I mine with CPU only?

Yes. Build with `-DCUDA_ENABLE=OFF -DOpenCL_ENABLE=OFF` and the miner uses CPU threads. GPU mining is significantly faster for CryptoNight-GPU, but CPU mining works for testing or small setups.

## My antivirus flags the miner

This is a false positive. Mining software is commonly flagged by antivirus heuristics. The source is open — you can verify it yourself and build from source.

## How do I configure my pool?

Edit `pools.txt` (created on first run) or use command line flags:

```bash
./n0s-cngpu -o pool.ryo-currency.com:3333 -u YOUR_WALLET -p x
```

## Where do I get a RYO wallet?

Visit [ryo-currency.com](https://ryo-currency.com) for wallet downloads and documentation.

## How do I check my hashrate?

- Watch the console output (set `verbose_level` to `4` in `config.txt`)
- Enable the HTTP API: set `"httpd_port" : 8080` in `config.txt`, then visit `http://localhost:8080/api.json`
- Use `--benchmark 10` for offline benchmarking

## Build fails with "internal compiler error: Killed"

Not enough RAM. Need at least 1 GB free. Try `make -j1` to reduce memory usage.

## What Linux distros are supported?

Officially tested on Ubuntu 18.04, 20.04, 22.04, and 24.04 LTS. Other distros with GCC 7+ and CMake 3.10+ should work.
