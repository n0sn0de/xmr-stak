# How to Use n0s-cngpu

## First Run

Start the miner:

```bash
./n0s-cngpu
```

On first run, the interactive setup wizard creates these config files:

- `config.txt` — general miner settings (HTTP API, logging, etc.)
- `pools.txt` — pool address, wallet, and connection settings
- `cpu.txt` — CPU backend thread configuration
- `amd.txt` — AMD/OpenCL GPU configuration (if OpenCL backend built)
- `nvidia.txt` — NVIDIA/CUDA GPU configuration (if CUDA backend built)

## Pool Configuration

Edit `pools.txt` to set your pool and wallet:

```json
"pool_list" :
[
    {
        "pool_address" : "pool.ryo-currency.com:3333",
        "wallet_address" : "YOUR_RYO_WALLET_ADDRESS",
        "rig_id" : "",
        "pool_password" : "x",
        "use_nicehash" : false,
        "use_tls" : false,
        "tls_fingerprint" : "",
        "pool_weight" : 1
    }
],
```

Multiple pools can be added for failover — the miner uses `pool_weight` to prioritize.

## Command Line Options

Run `./n0s-cngpu --help` for all options. Key ones:

```
  -o, --url URL        Pool address (host:port)
  -u, --user ADDR      Wallet address
  -p, --pass PASS      Pool password (default: x)
  -r, --rigid ID       Rig identifier for pool stats
  -c, --config FILE    Path to config.txt
  --poolconf FILE      Path to pools.txt
  --cpu FILE           Path to cpu.txt
  --amd FILE           Path to amd.txt
  --nvidia FILE        Path to nvidia.txt
  --benchmark          60-second offline benchmark
  --noCPU              Disable CPU mining
  --noAMD              Disable AMD GPU mining
  --noNVIDIA           Disable NVIDIA GPU mining
  --noUAC              Skip UAC prompt
  -h, --help           Show help
```

## Backend Selection

Disable backends you don't need:

```bash
# GPU only (no CPU mining)
./n0s-cngpu --noCPU

# AMD GPU only
./n0s-cngpu --noCPU --noNVIDIA

# NVIDIA GPU only
./n0s-cngpu --noCPU --noAMD

# CPU only
./n0s-cngpu --noAMD --noNVIDIA
```

## HTTP API

Enable the built-in monitoring API in `config.txt`:

```json
"httpd_port" : 8080,
```

Endpoints:

- `http://localhost:8080/api.json` — hashrate, pool connection stats, results
- `http://localhost:8080/h` — HTML dashboard

## Benchmarking

Run a 60-second offline benchmark (no pool connection needed):

```bash
./n0s-cngpu --benchmark 10
```

The number is the CryptoNight block version (use 10 for CryptoNight-GPU).

## Verbose Logging

Set `verbose_level` in `config.txt`:

- `3` — normal (default)
- `4` — detailed hashrate reports
- `5+` — debug output

Set `h_print_time` for hashrate report interval (seconds).
