# Pool & Network Protocol Documentation

**How the miner talks to pools: from TCP socket to accepted shares.**

---

## Architecture Overview

```
┌──────────────┐     events     ┌──────────────┐     stratum     ┌──────────┐
│  GPU Threads │ ──────────────→│   executor   │ ──────────────→│   Pool   │
│  (backends)  │ ←──────────────│  (event loop)│ ←──────────────│  Server  │
│              │   new jobs     │              │   JSON-RPC      │          │
└──────────────┘                └──────────────┘                └──────────┘
                                       │
                                       ├── jpsock (stratum client)
                                       ├── base_socket (TCP/TLS)
                                       └── globalStates (job broadcast)
```

### Key Files

| File | Role | Lines |
|------|------|-------|
| `net/jpsock.cpp/hpp` | Stratum JSON-RPC client | ~935 |
| `net/socket.cpp/hpp` | TCP + TLS transport | ~456 |
| `net/msgstruct.hpp` | Message types + event union | ~243 |
| `net/socks.hpp` | Platform socket abstraction | ~99 |
| `misc/executor.cpp/hpp` | Central event loop + pool manager | ~1464 |
| `backend/globalStates.*` | Job broadcast to mining threads | ~127 |
| `backend/miner_work.hpp` | Work unit passed to miners | ~111 |
| `backend/pool_data.hpp` | Pool metadata (MOTD, etc.) | ~23 |

---

## Stratum Protocol (jpsock)

The miner uses **Stratum over TCP** (optionally TLS) to communicate with pools.
All messages are newline-delimited JSON-RPC 1.0 style.

### Connection Flow

```
Miner                                      Pool
  │                                          │
  │──── TCP connect (or TLS handshake) ────→│
  │                                          │
  │──── login {"method":"login"} ──────────→│
  │                                          │
  │←─── login response + first job ─────────│
  │     {id, job: {job_id, blob, target}}    │
  │                                          │
  │←─── job notification ──────────────────│  (server push)
  │     {"method":"job", "params":{...}}     │
  │                                          │
  │──── submit {"method":"submit"} ────────→│
  │     {id, job_id, nonce, result}          │
  │                                          │
  │←─── submit response ──────────────────│
  │     {result: {status: "OK"}}             │
  │         or                               │
  │     {error: {message: "..."}}            │
  │                                          │
```

### Login (cmd_login)

```json
{
  "method": "login",
  "params": {
    "login": "<wallet_address>",
    "pass": "<password>",
    "rigid": "<rig_id>",
    "agent": "n0s-ryo-miner/1.0.0/<git>/<branch>/Linux/..."
  },
  "id": 1
}
```

Response includes:
- `id` — Miner ID for future submits
- `job` — First mining job
- `extensions` — Server capabilities: `algo`, `backend`, `hashcount`, `motd`

### Job Notification (server → miner)

```json
{
  "method": "job",
  "params": {
    "job_id": "abc123...",
    "blob": "0707...hex...",
    "target": "b88d0600",
    "height": 123456,
    "motd": "hex-encoded-message"
  }
}
```

**Key fields:**
- `blob` — Hex-encoded block template (typically 76 bytes for RYO). First 4 bytes at offset 39 are the nonce field.
- `target` — Difficulty target. Can be 4-byte (32-bit) or 8-byte (64-bit) hex, little-endian.
- `height` — Block height (byte-swapped for internal use via `bswap_64`).
- `motd` — Optional hex-encoded pool message of the day.

**Target conversion:** A 32-bit target `t` is expanded to 64-bit via:
```
target64 = 0xFFFFFFFFFFFFFFFF / (0xFFFFFFFF / t)
```
Difficulty = `0xFFFFFFFFFFFFFFFF / target64`

### Share Submission (cmd_submit)

```json
{
  "method": "submit",
  "params": {
    "id": "<miner_id>",
    "job_id": "<job_id>",
    "nonce": "deadbeef",
    "result": "64-char-hex-hash",
    "backend": "AMD GPU 0",
    "hashcount": 12345,
    "hashcount_total": 67890,
    "algo": "cryptonight_gpu",
    "base_algo": "cryptonight_gpu",
    "iterations": "0x0000c000",
    "scratchpad": "0x00200000",
    "mask": "0x001fffc0"
  },
  "id": 1
}
```

Extension fields (`backend`, `hashcount*`, `algo*`) are only sent if the server
advertised the corresponding capability in the login response `extensions` array.

### Threading Model

`jpsock` uses a **dedicated receive thread** (`jpsock_thread`) that:
1. Reads newline-delimited JSON from the socket
2. Parses each line in-place (`ParseInsitu`)
3. Routes to either:
   - **Job notification** → `process_pool_job()` → pushes `EV_POOL_HAVE_JOB` event
   - **Call response** → Wakes the calling thread via condition variable

**Critical assumption:** Only ONE thread sends commands (the executor thread).
The receive thread only reads. This avoids needing locks on the send path.

### Memory Management

JSON parsing uses **pre-allocated memory pools** (rapidjson `MemoryPoolAllocator`):
- `bJsonRecvMem` (4KB) — Receive-side document parsing
- `bJsonParseMem` (4KB) — Parse allocator (temp data)
- `bJsonCallMem` (4KB) — Call response data (for the calling thread)

This avoids heap allocation on the hot path (every incoming message).

---

## Transport Layer (socket.cpp)

Two implementations of `base_socket`:
- **`plain_socket`** — Raw TCP via POSIX sockets
- **`tls_socket`** — TLS via OpenSSL BIO (compile-time optional via `CONF_NO_TLS`)

### Connection Process (plain_socket)

1. Parse `host:port` from pool address
2. `getaddrinfo()` → collect IPv4 and IPv6 results
3. Random selection from available addresses (IPv4 preferred if configured)
4. Create socket + `TCP_NODELAY` (disable Nagle's algorithm for low latency)
5. `connect()`

### TLS Handshake

1. Create SSL context (`SSLv23_method` with SSLv2/v3/TLSv1 disabled if `tls_secure_algo=true`)
2. BIO connect + handshake
3. Verify server certificate presented
4. SHA-256 fingerprint check against configured `tls_fingerprint` (if set)
5. Log fingerprint if not configured (for user to pin later)

### Error Handling

All socket errors flow through `jpsock::set_socket_error()` which:
- Sets an atomic error flag (first error wins)
- Stores the error message string
- The receive thread detects errors and pushes `EV_SOCK_ERROR` to executor

---

## Event System (executor + msgstruct)

The executor is the **central coordinator** — a single-threaded event loop that
processes all mining events through a thread-safe queue (`thdq<ex_event>`).

### Event Types (ex_event_name)

| Event | Source | Action |
|-------|--------|--------|
| `EV_SOCK_READY` | jpsock recv thread | Attempt login on this pool |
| `EV_SOCK_ERROR` | jpsock recv thread | Log error, reconnect to pool |
| `EV_GPU_RES_ERROR` | GPU backend threads | Log GPU error |
| `EV_POOL_HAVE_JOB` | jpsock recv thread | Broadcast new work to all miners |
| `EV_MINER_HAVE_RESULT` | Mining threads | Submit share to pool |
| `EV_PERF_TICK` | Clock thread | Print hashrate periodically |
| `EV_EVAL_POOL_CHOICE` | Timed event | Re-evaluate which pool to mine on |
| `EV_USR_HASHRATE` | User input ('h') | Print hashrate to console |
| `EV_USR_RESULTS` | User input ('r') | Print results summary |
| `EV_USR_CONNSTAT` | User input ('c') | Print connection status |
| `EV_HASHRATE_LOOP` | Timed event | Periodic hashrate logging |
| `EV_HTML_*` | HTTP API | Generate HTML/JSON reports |

### ex_event Union

`ex_event` uses a **discriminated union** for zero-allocation event passing:
```cpp
union {
    pool_job oPoolJob;       // New mining job from pool
    job_result oJobResult;   // Miner found a valid share
    sock_err oSocketError;   // Socket/connection error
    gpu_res_err oGpuError;   // GPU computation error
};
```
Move-only semantics. Copy is deleted. The union is discriminated by `iName`.

### Event Loop (ex_main)

```
executor::ex_main():
    1. Start all backends (GPU threads)
    2. Connect to configured pools
    3. Loop forever:
        event = oEventQ.pop()  // blocking wait
        switch(event.type):
            SOCK_READY    → cmd_login(), broadcast first job
            SOCK_ERROR    → log, disconnect, schedule reconnect
            POOL_HAVE_JOB → globalStates::switch_work() (broadcast to miners)
            MINER_RESULT  → jpsock::cmd_submit()
            PERF_TICK     → print hashrate
            EVAL_POOL     → pick best pool by weight
            ...
```

### Clock Thread

A separate thread (`ex_clock_thd`) generates timed events:
- Ticks every 500ms (`iTickTime`)
- Decrements counters on timed events
- When a counter reaches zero, pushes the event to the main queue
- Used for: hashrate printing, pool reconnect delays, pool evaluation

---

## Job Dispatch Pipeline

When a new job arrives from the pool:

```
Pool ──→ jpsock::process_pool_job()
              │
              ├── Parse job_id, blob, target, height
              ├── Validate and store in oCurrentJob
              └── Push EV_POOL_HAVE_JOB to executor
                        │
                        ▼
         executor::on_pool_have_job()
              │
              ├── Create miner_work from pool_job
              ├── Store pool metadata (MOTD, etc.)
              └── globalStates::switch_work(miner_work, pool_data)
                        │
                        ▼
         globalStates::switch_work()
              │
              ├── Store new work under write lock
              ├── Increment work counter (iGlobalJobNo)
              └── All mining threads detect counter change
                        │
                        ▼
         Mining thread (GPU backend)
              │
              ├── Detect new job (compare local vs global job number)
              ├── Copy work blob locally
              ├── Set nonce range for this thread
              └── Begin hashing with new data
```

### miner_work Structure

```cpp
struct miner_work {
    char sJobID[128];        // Pool job ID
    uint8_t bWorkBlob[128];  // Block template (nonce at offset 39)
    uint32_t iWorkSize;      // Blob length (typically 76)
    uint64_t iTarget;        // 64-bit difficulty target
    bool bNiceHash;          // Limit nonce to 3 bytes
    size_t iPoolId;          // Which pool this job came from
    uint64_t iBlockHeight;   // For fork-height checks (byte-swapped)
};
```

### Nonce Management

Each mining thread gets a **nonce range** to avoid collisions:
- Thread 0: nonce starts at `iSavedNonce + 0`
- Thread 1: nonce starts at `iSavedNonce + (2^24)` (NiceHash) or higher
- Each thread increments by `intensity` (number of hashes per kernel launch)
- When a share is found, the pool's saved nonce is updated to prevent replays

---

## Multi-Pool Support

The miner supports **multiple pools with weighted failover**:

### Pool Selection Algorithm (eval_pool_choice)

Each pool has a **gross weight** computed as:
```
weight = base_weight                    // from config
       + 10.0 if socket is connected
       + 10.0 if login succeeded
```

The pool with the highest gross weight is selected. Ties favor the currently active pool (hysteresis).

### Failover Flow

1. All pools are connected simultaneously
2. Mining happens on the highest-weight pool
3. If the active pool disconnects → immediate switch to next-best
4. Reconnect attempts use exponential backoff
5. When the preferred pool reconnects → switch back (higher base weight)

### Pool Connection Lifecycle

```
                    ┌─────────┐
                    │  INIT   │
                    └────┬────┘
                         │ set_hostname()
                    ┌────▼────┐
                    │RESOLVING│ DNS lookup + socket create
                    └────┬────┘
                         │ connect()
                    ┌────▼────┐
                    │CONNECTED│ TCP/TLS established
                    └────┬────┘
                         │ EV_SOCK_READY → cmd_login()
                    ┌────▼────┐
                    │LOGGED IN│ Mining actively on this pool
                    └────┬────┘
                         │ error or disconnect
                    ┌────▼────┐
                    │  ERROR  │ Log, schedule reconnect
                    └────┬────┘
                         │ timed event
                         └──→ back to INIT
```

---

## Share Submission Pipeline

When a GPU thread finds a hash below the target:

```
GPU Kernel ──→ Backend minethd::result_found()
                    │
                    ├── Create job_result{job_id, nonce, hash, thread_id, algo}
                    └── executor::push_event(EV_MINER_HAVE_RESULT)
                              │
                              ▼
               executor::on_miner_result()
                    │
                    ├── Verify hash against target (CPU-side double-check)
                    │   └── If mismatch: log "GPU COMPUTE ERROR" (bad hardware?)
                    ├── Log share statistics
                    └── jpsock::cmd_submit()
                              │
                              ├── Serialize JSON-RPC submit
                              ├── Send over socket
                              ├── Wait for response (with timeout)
                              └── Return success/failure
                                        │
                              ┌─────────┴─────────┐
                              │                     │
                         "OK" response         Error response
                              │                     │
                     log_result_ok()        log_result_error()
                     increment [OK] tally   increment error tally
```

### CPU-Side Hash Verification

The executor performs an **independent CPU-side hash** of every share before
submitting to the pool. This catches GPU computation errors early:
- Corrupted GPU memory
- Driver bugs
- Overclocking instability

If the CPU hash doesn't match the GPU hash, the share is logged as a
"GPU COMPUTE ERROR" and NOT submitted to the pool.

---

## Configuration (pools.txt)

```json
"pool_list": [
    {
        "pool_address": "pool.ryo-currency.com:3333",
        "wallet_address": "RYO...",
        "rig_id": "my-rig",
        "pool_password": "x",
        "use_nicehash": false,
        "use_tls": false,
        "tls_fingerprint": "",
        "pool_weight": 1
    }
]
```

| Field | Description |
|-------|-------------|
| `pool_address` | `host:port` format. Stratum only (no HTTP). |
| `wallet_address` | RYO wallet address or pool login. |
| `rig_id` | Optional identifier for pool-side stats. |
| `pool_password` | Usually `x` or empty. |
| `use_nicehash` | Limit nonce to 3 bytes (NiceHash compatibility). |
| `use_tls` | Enable TLS encryption. |
| `tls_fingerprint` | SHA256 fingerprint for cert pinning. Empty = trust any cert. |
| `pool_weight` | Priority weight. Higher = preferred. Must be > 0. |

### CLI Override

Pool can be specified via command line, overriding `pools.txt`:
```
./n0s-ryo-miner -o pool.example.com:3333 -u WALLET -p x
```

---

## Error Handling Summary

| Error Type | Handled By | Recovery |
|-----------|-----------|---------|
| DNS resolution failure | `set_hostname()` | Retry with backoff |
| TCP connect failure | `connect()` | Retry with backoff |
| TLS handshake failure | `tls_socket::connect()` | Retry with backoff |
| TLS fingerprint mismatch | `tls_socket::connect()` | Fatal (won't retry) |
| JSON parse error | `process_line()` | Disconnect + reconnect |
| Login rejected | `cmd_login()` | Log error, try next pool |
| Share rejected | `cmd_submit()` | Log, continue mining |
| Socket read error | `recv()` | Disconnect + reconnect |
| Socket write error | `send()` | Disconnect + reconnect |
| Call timeout | `cmd_ret_wait()` | Disconnect + reconnect |
| GPU compute error | `on_miner_result()` | Log warning, don't submit |

---

*This document describes the pool/network layer as of Session 4 of the rewrite.
The protocol implementation is inherited from xmr-stak and works correctly with
all RYO Currency pools supporting the Stratum protocol.*
