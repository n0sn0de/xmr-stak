# Polish Plan

**From Optimized Engine to Shipped Product**

*Status: Active. Pillar 1 complete (Session 50). Pillar 2 dashboard shipped as v3.2.0 (Session 54). Pillar 3.1 platform abstraction complete (Session 56). v3.2.0 released with single binary + GUI dashboard.*

---

## Vision

The engine is fast. The algorithm is bit-exact. The autotune finds optimal settings on any GPU. What remains is the **last mile** — a miner that deploys as a single file, presents a modern face, and runs on every platform miners actually use.

Three pillars, in dependency order:

1. **Single Executable** — Eliminate the 3-file deployment (main + 2 .so plugins). One binary, zero `dlopen`.
2. **GUI Dashboard** — Embedded web UI for real-time hashrate visualization, pool configuration, and GPU telemetry. Ships inside the same executable. CLI stays first-class.
3. **Windows Support** — Cross-platform abstraction layer, MSVC builds, Windows GPU telemetry, CI/CD matrix producing Linux + Windows releases.

Each pillar is self-contained. Each ships independently. Each is validated before the next begins.

---

## Principles

1. **Ship incrementally** — Each pillar produces a working, releasable artifact
2. **CLI is sacred** — The GUI is additive; headless/CLI operation never degrades
3. **One binary, zero friction** — Download → run. No library paths, no installers, no runtime deps
4. **Lightweight by default** — No Electron, no Qt. Embedded web assets + system webview ≤ 2 MB overhead
5. **Measure the cost** — Track binary size, startup time, and memory footprint at every phase gate
6. **Bit-exact always** — Mining output is identical regardless of CLI/GUI/platform

---

## Branch Strategy

Same discipline as the optimization phase:

1. Master stays stable (release-ready always)
2. Focused branches: `polish/single-binary`, `polish/gui-dashboard`, `polish/windows-support`
3. Test: golden hashes + container builds + live mining on all 3 GPUs
4. Merge only when validated ✅

---

## Pillar 1: Single Executable

**Goal:** One statically-linked binary. Download it, `chmod +x`, run. No `.so` files, no `LD_LIBRARY_PATH`.

**Current state:** The build produces 3 files:
- `n0s-ryo-miner` — main executable (links `n0s-backend` static lib)
- `libn0s_cuda_backend.so` — CUDA backend, loaded at runtime via `dlopen()`
- `libn0s_opencl_backend.so` — OpenCL backend, loaded at runtime via `dlopen()`

The `plugin.hpp` system (`dlopen`/`dlsym`) exists because xmr-stak originally supported dozens of algorithm combinations with optional backends. With our single-algorithm focus (CryptoNight-GPU) and the always-on CUDA + OpenCL build matrix, this indirection is pure overhead.

### 1.1 — Static Backend Linking

| Task | Detail |
|------|--------|
| Convert CUDA backend to static lib | `cuda_add_library(n0s_cuda_backend STATIC ...)` instead of SHARED |
| Convert OpenCL backend to static lib | `add_library(n0s_opencl_backend STATIC ...)` instead of SHARED |
| Direct function calls | Replace `dlsym("n0s_start_backend")` with direct `n0s::cuda::startBackend()` / `n0s::opencl::startBackend()` calls |
| Remove `plugin.hpp` | Delete the `dlopen`/`dlsym`/`dlclose` abstraction entirely |
| Update `backendConnector.cpp` | Call backend `startBackend()` directly, guarded by `#ifndef CONF_NO_CUDA` / `#ifndef CONF_NO_OPENCL` |
| Remove `-ldl` dependency | No longer needed once `dlopen` is eliminated |
| CMake install simplification | Single binary install, no LIBRARY DESTINATION needed |

**Risk:** CUDA's `nvcc` produces object files that link against `libcudart`. Static linking requires `libcudart_static.a` (ships with CUDA toolkit). Use `CUDA_USE_STATIC_CUDA_RUNTIME ON`.

### 1.2 — Embed OpenCL Kernel Sources

Currently, OpenCL `.cl` files are read from disk at runtime (or compiled and cached). For single-binary deployment, embed them.

| Task | Detail |
|------|--------|
| Generate C++ string literals from `.cl` files | CMake `file(READ ...)` + `configure_file()` at build time, or `xxd -i` |
| Create `opencl_kernels_embedded.hpp` | `constexpr` string views for `cryptonight.cl`, `cryptonight_gpu.cl`, `wolf-aes.cl` |
| Update `gpu.cpp` kernel compile path | Load from embedded strings instead of filesystem |
| Keep OpenCL cache | Compiled kernel binaries still cached in `~/.openclcache/` for fast startup |

### 1.3 — Static Dependency Linking

| Dependency | Static Strategy |
|------------|----------------|
| OpenSSL | Link `libssl.a` + `libcrypto.a` (CMake `OPENSSL_USE_STATIC_LIBS ON`) |
| microhttpd | Link `libmicrohttpd.a` |
| hwloc | Link `libhwloc.a` |
| CUDA runtime | `cudart_static` (NVIDIA provides this) |
| OpenCL | `libOpenCL.so` remains dynamic — it's a system ICD loader, not embeddable |
| libstdc++ | `-static-libstdc++ -static-libgcc` (already supported via `CMAKE_LINK_STATIC`) |

**OpenCL exception:** `libOpenCL.so` is an ICD (Installable Client Driver) loader that dispatches to vendor drivers. It *must* be dynamically linked. This is standard — every OpenCL application does this. The binary still "just works" on any system with an OpenCL driver installed.

### 1.4 — Build Matrix Update

| Build Variant | Contents |
|---------------|----------|
| `n0s-ryo-miner-cuda-opencl` | Full: CUDA + OpenCL in one binary |
| `n0s-ryo-miner-cuda` | CUDA-only (no OpenCL dependency) |
| `n0s-ryo-miner-opencl` | OpenCL-only (no CUDA dependency) |

Container builds (`scripts/container-build.sh`) updated to produce single files instead of tarballs.

### 1.5 — Validation

- [ ] Single binary runs without any `.so` files present
- [ ] Golden hash test vectors pass
- [ ] Live mining: 100% share acceptance on all 3 GPUs
- [ ] Binary size delta documented (expected: +5–15 MB from static CUDA runtime)
- [ ] Startup time unchanged (OpenCL cache still works)

### Estimated Scope

~4 sessions. Low risk — purely build-system and linkage changes, zero algorithm code touched.

---

## Pillar 2: GUI Dashboard

**Goal:** A modern, lightweight, n0s-ryo-branded web dashboard embedded in the miner binary. Launch with `--gui` or run headless — same binary, same engine.

### Design Philosophy

The miner already has an HTTP daemon (`microhttpd`) serving a basic status page. We evolve this into a proper REST API backend + modern single-page application frontend, all compiled into the executable as embedded assets. No external web server. No Electron. No runtime JavaScript engine. Just the miner serving its own dashboard on `localhost`.

**Architecture:**

```
┌─────────────────────────────────────────────────────────┐
│                     n0s-ryo-miner                       │
│                                                         │
│  ┌─────────┐   ┌─────────────┐   ┌──────────────────┐  │
│  │  Mining  │   │  REST API   │   │  Embedded Web    │  │
│  │  Engine  │◄──┤  (JSON)     │──►│  Assets (HTML/   │  │
│  │          │   │  /api/v1/*  │   │  CSS/JS gzipped) │  │
│  └─────────┘   └──────┬──────┘   └──────────────────┘  │
│                        │                                 │
│                   localhost:port                          │
└────────────────────────┼─────────────────────────────────┘
                         │
              ┌──────────┴──────────┐
              │   Browser / Webview  │
              │   (system-provided)  │
              └─────────────────────┘
```

- `--gui` flag: starts mining + opens system default browser to `http://localhost:{port}`
- `--httpd-port` (existing): serves dashboard on custom port
- No `--gui` flag: pure CLI mode, dashboard still available if HTTP port is configured
- The frontend is a static SPA — all state comes from polling REST endpoints

### 2.1 — REST API Layer

Replace the current monolithic HTML-rendering `get_http_report()` with clean JSON endpoints.

| Endpoint | Method | Response |
|----------|--------|----------|
| `GET /api/v1/status` | — | Mining state, uptime, connection status |
| `GET /api/v1/hashrate` | — | Per-GPU and total hashrate (10s, 60s, 15m windows) |
| `GET /api/v1/hashrate/history` | — | Time-series hashrate data (last 1h, 1-second resolution) |
| `GET /api/v1/gpus` | — | GPU list with telemetry (temp, power, fan, clocks) |
| `GET /api/v1/pool` | — | Current pool, accepted/rejected shares, difficulty |
| `GET /api/v1/config` | — | Current miner configuration (sanitized — no passwords) |
| `PUT /api/v1/config/pool` | JSON | Update pool settings (apply on next reconnect) |
| `GET /api/v1/version` | — | Version, build info, backends enabled |

**Implementation notes:**
- Reuse existing `executor::get_http_report()` data, serialize to JSON via rapidjson (already vendored)
- Existing `telemetry` class already tracks per-thread hashrate with time bucketing
- `GpuTelemetry` struct already has temp/power/fan/clocks
- Add a circular buffer (ring buffer) for hashrate history — ~3600 entries × ~32 bytes = ~115 KB
- Authentication: keep existing digest auth, add optional bearer token for API access
- CORS headers for local development

### 2.2 — Frontend SPA

A single-page application built with vanilla HTML/CSS/JS (no framework). Keeps the binary lean and eliminates build toolchain complexity.

**Dashboard Layout:**

```
┌─────────────────────────────────────────────────────────────┐
│  ╔═══ N0S-RYO ═══╗       Connected: pool.ryo.org:3333  🟢  │
│  ║  GPU MINER    ║       v3.2.0 • 2h 14m uptime            │
│  ╚════════════════╝                                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   Total Hashrate          Shares           Difficulty        │
│   ┌─────────────┐   ┌──────────────┐   ┌──────────────┐    │
│   │  8,932 H/s  │   │  247 / 0     │   │  185,000     │    │
│   │  ▲ 0.3%     │   │  acc / rej   │   │              │    │
│   └─────────────┘   └──────────────┘   └──────────────┘    │
│                                                              │
│   Hashrate ─────────────────────────────────────────────    │
│   9.5k ┤                                                     │
│   9.0k ┤    ╭──────╮    ╭─────────────────╮                 │
│   8.5k ┤───╯       ╰───╯                  ╰────            │
│   8.0k ┤                                                     │
│        └──────────────────────────────────────────── time    │
│         -60m        -45m       -30m       -15m       now     │
│                                                              │
├─────────────────────────────────────────────────────────────┤
│   GPU Details                                                │
│  ┌───┬──────────────────┬────────┬───────┬───────┬────────┐ │
│  │ # │ Device           │  H/s   │ Temp  │ Power │  Fan   │ │
│  ├───┼──────────────────┼────────┼───────┼───────┼────────┤ │
│  │ 0 │ RX 9070 XT       │ 5,069  │ 62°C  │ 186W  │  72%  │ │
│  │ 1 │ GTX 1070 Ti      │ 1,631  │ 58°C  │ 120W  │  45%  │ │
│  │ 2 │ RTX 2070         │ 2,236  │ 55°C  │ 135W  │  38%  │ │
│  └───┴──────────────────┴────────┴───────┴───────┴────────┘ │
│                                                              │
├─────────────────────────────────────────────┬────────────────┤

```

**Visual Identity:**
- Dark theme primary — `#1a1a2e` background, `#16213e` cards
- RYO blue → cyan gradient accent (matching CLI banner: `#005f87` → `#00d7ff`)
- Monospace numerics for hashrate/telemetry (tabular alignment)
- Minimal animation — hashrate chart updates smoothly, no gratuitous motion
- Responsive: works on mobile for remote monitoring

**Components:**

| Component | Technology | Notes |
|-----------|-----------|-------|
| Hashrate chart | `<canvas>` + lightweight charting (~5 KB) | No Chart.js (200 KB). Use [uPlot](https://github.com/leeoniya/uPlot) (~35 KB) or hand-rolled canvas |
| GPU telemetry table | Vanilla DOM | Updated via polling `/api/v1/gpus` every 2s |
| Pool config editor | HTML `<form>` | PUT to `/api/v1/config/pool`, validated client-side |
| Share counter | Animated number | CSS transition on update |
| Connection indicator | SVG dot | Green/yellow/red based on pool state |

**Tech stack reasoning:**
- **No React/Vue/Svelte** — framework overhead (50–200 KB gzipped) not justified for a single-page dashboard
- **No npm/node build step** — the frontend is ~3 files (HTML + CSS + JS) that get embedded at compile time
- **uPlot** for charting — 35 KB unminified, GPU-accelerated canvas rendering, purpose-built for time-series. Or skip the dependency entirely and draw on `<canvas>` directly (~200 lines of JS for a basic scrolling line chart)
- **Total frontend size target:** < 50 KB gzipped (embedded in binary)

### 2.3 — Asset Embedding

| Task | Detail |
|------|--------|
| Build-time asset pipeline | CMake custom command: minify HTML/CSS/JS → gzip → `xxd -i` → C++ header |
| `embedded_assets.hpp` | `constexpr` arrays: `gui_index_html_gz[]`, `gui_style_css_gz[]`, `gui_app_js_gz[]` |
| HTTP response with `Content-Encoding: gzip` | Serve pre-compressed assets directly — zero runtime compression |
| Fallback for development | `--gui-dev` flag serves from filesystem (hot reload during development) |
| ETag caching | Version-based ETags (already implemented for current CSS) |

### 2.4 — GUI Launch Mode

| Flag | Behavior |
|------|----------|
| (no flag) | CLI mode, HTTP daemon on configured port (existing behavior) |
| `--gui` | Start miner + open `http://localhost:{port}` in default browser |
| `--gui-port PORT` | Override HTTP port for GUI mode (default: 9090) |
| `--gui-dev` | Serve frontend from `gui/` directory instead of embedded assets |

**Browser launch:** `xdg-open` on Linux, `start` on Windows (see Pillar 3). Simple, no webview dependency.

For users who want a native window experience, we provide an optional webview wrapper as a separate lightweight binary (`n0s-ryo-gui`) that embeds the system webview (WebKitGTK on Linux, WebView2 on Windows). This is **not** in the critical path — the browser-based approach works out of the box.

### 2.5 — Hashrate History Ring Buffer

```cpp
namespace n0s {
struct HashrateHistory {
    struct Sample {
        uint64_t timestamp_ms;   // Unix timestamp
        double total_hs;         // Total hashrate
        double per_gpu_hs[16];   // Per-GPU hashrate (max 16 GPUs)
    };
    
    static constexpr size_t CAPACITY = 3600;  // 1 hour at 1s resolution
    std::array<Sample, CAPACITY> samples;
    size_t head = 0;
    size_t count = 0;
};
} // namespace n0s
```

~115 KB memory. Populated by `executor` on the existing 1-second tick. Served as JSON array by `/api/v1/hashrate/history`. The chart component requests this once on load, then appends from `/api/v1/hashrate` polling.

### 2.6 — Validation

- [x] Dashboard loads in Chrome, Firefox, Safari
- [x] Hashrate chart renders real-time data correctly
- [x] Pool config changes apply on next pool reconnect (PUT /api/v1/config/pool)
- [x] GPU telemetry updates every 2 seconds
- [x] All API endpoints return valid JSON (10 endpoints: 9 GET + 1 PUT)
- [x] Frontend total size < 50 KB gzipped (6.1 KB — 12% of target)
- [x] `--gui` opens browser and mines simultaneously
- [x] CLI-only mode unaffected (no GUI overhead when not using `--gui`)
- [ ] Authentication works (digest auth on API endpoints)

### Estimated Scope

~8–10 sessions. Medium risk — new frontend code, but mining engine untouched.

---

## Pillar 3: Windows Support

**Goal:** First-class Windows builds for both CLI and GUI. Download `.exe`, double-click, mine.

**Current Linux-only surfaces:**

| Component | Linux-specific | Windows equivalent |
|-----------|---------------|-------------------|
| `plugin.hpp` | `dlopen`/`dlsym`/`dlclose` | **Eliminated** by Pillar 1 (static linking) |
| `gpu_telemetry.cpp` | `/sys/class/drm/` (sysfs) | NVML API (NVIDIA), ADL/AGS SDK (AMD) |
| `gpu_telemetry.cpp` | `nvidia-smi` subprocess | NVML direct API calls |
| `socket.cpp` | POSIX sockets | Winsock2 (`WSAStartup`, etc.) |
| `home_dir.hpp` | `$HOME`, `getenv("HOME")` | `%APPDATA%`, `SHGetKnownFolderPath` |
| `console.cpp` | ANSI escape codes | Windows Console Virtual Terminal (Win10+) or `SetConsoleTextAttribute` |
| `banner.cpp` | ANSI + Unicode box drawing | Same — Windows Terminal supports this since 2019 |
| OpenCL cache path | `~/.openclcache/` | `%LOCALAPPDATA%\n0s\openclcache\` |
| Config paths | `config.txt` in CWD | Same, but also support `%APPDATA%\n0s\` |
| Signals | `SIGINT`/`SIGTERM` | `SetConsoleCtrlHandler` |
| Thread naming | `pthread_setname_np` | `SetThreadDescription` (Win10 1607+) |
| CMake | GCC/Clang assumed | MSVC + Clang-CL + Ninja |

### 3.1 — Platform Abstraction Layer ✅ (Session 56)

Introduce a thin `n0s/platform/` module — not an abstraction framework, just the handful of functions that differ:

```
n0s/platform/
├── platform.hpp           ← Common interface (inline where trivial)
├── platform_linux.cpp     ← Linux implementations
├── platform_windows.cpp   ← Windows implementations
```

| Function | Purpose |
|----------|---------|
| `openBrowser(url)` | `xdg-open` / `ShellExecuteW` |
| `getConfigDir()` | `$HOME/.config/n0s/` / `%APPDATA%\n0s\` |
| `getCacheDir()` | `$HOME/.cache/n0s/` / `%LOCALAPPDATA%\n0s\` |
| `getHomePath()` | `$HOME` / `%USERPROFILE%` |
| `setThreadName(name)` | `pthread_setname_np` / `SetThreadDescription` |
| `setupSignalHandlers()` | `sigaction` / `SetConsoleCtrlHandler` |
| `enableConsoleColors()` | no-op / `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)` |

Everything else (sockets, GPU telemetry) is handled by existing cross-platform libraries or dedicated modules below.

### 3.2 — Network Layer (Winsock)

The socket code in `n0s/net/socket.cpp` uses POSIX sockets. Two paths:

**Option A (preferred): Minimal `#ifdef` shims**
- `WSAStartup`/`WSACleanup` init in `main()`
- `closesocket()` instead of `close()` on Windows
- `#include <winsock2.h>` / `<ws2tcpip.h>` instead of `<sys/socket.h>`
- OpenSSL works identically on both platforms

**Option B: Adopt a cross-platform socket library (e.g., Boost.Asio)**
- Overkill for our needs — the socket code is ~400 lines and well-contained

### 3.3 — GPU Telemetry (Windows)

| Backend | Linux (current) | Windows |
|---------|-----------------|---------|
| NVIDIA | `nvidia-smi` subprocess parse | **NVML API** (direct C calls, `nvml.h`) |
| AMD | sysfs `/sys/class/drm/cardN/` | **ADL SDK** (AMD Display Library) or **AMDSMI** |

**NVML** ships with every NVIDIA driver. Link dynamically (`nvml.dll`). Provides temp, power, fan, clocks, utilization — everything we need.

**ADL/AMDSMI** ships with AMD drivers. Same story. Provides equivalent telemetry.

Both are also useful on Linux as replacements for the current subprocess/sysfs approach (more reliable, lower overhead). Consider migrating Linux telemetry to NVML/AMDSMI as well.

### 3.4 — Build System (Windows)

| Task | Detail |
|------|--------|
| CMake generator | Support Ninja + MSVC (`cmake -G Ninja -DCMAKE_CXX_COMPILER=cl`) |
| CUDA on Windows | NVCC + MSVC host compiler (standard CUDA toolkit setup) |
| OpenCL on Windows | Link against `OpenCL.lib` from GPU SDK or Khronos loader |
| OpenSSL on Windows | vcpkg: `vcpkg install openssl:x64-windows-static` |
| microhttpd on Windows | vcpkg: `vcpkg install libmicrohttpd:x64-windows-static` |
| hwloc on Windows | Optional — disable by default on Windows (`-DHWLOC_ENABLE=OFF`) |
| Static CRT | `/MT` flag for fully static executable |
| Warning flags | `-Wall -Wextra` (GCC/Clang) → `/W4` (MSVC) |

**vcpkg integration:**
```cmake
# CMakeLists.txt addition for Windows deps
if(WIN32)
    find_package(unofficial-libmicrohttpd CONFIG)
    # ... vcpkg handles the rest
endif()
```

### 3.5 — CI/CD Matrix

GitHub Actions workflow producing release artifacts:

| OS | GPU Backend | Artifact |
|----|-------------|----------|
| Linux x86_64 | CUDA 11.8 + OpenCL | `n0s-ryo-miner-linux-cuda11-opencl` |
| Linux x86_64 | CUDA 12.8 + OpenCL | `n0s-ryo-miner-linux-cuda12-opencl` |
| Linux x86_64 | OpenCL only | `n0s-ryo-miner-linux-opencl` |
| Windows x86_64 | CUDA 12.8 + OpenCL | `n0s-ryo-miner-windows-cuda12-opencl.exe` |
| Windows x86_64 | OpenCL only | `n0s-ryo-miner-windows-opencl.exe` |

Container builds (existing `scripts/container-build.sh`) for Linux. GitHub Actions Windows runners for Windows builds.

### 3.6 — Validation

- [ ] Windows build compiles clean with MSVC (zero warnings at `/W4`)
- [ ] Golden hash test vectors pass on Windows  
- [ ] Live mining: accepted shares on Windows with NVIDIA GPU
- [ ] Live mining: accepted shares on Windows with AMD GPU
- [ ] GUI dashboard works in Edge/Chrome on Windows
- [ ] `--gui` opens default browser on Windows
- [ ] Autotune works end-to-end on Windows
- [ ] GPU telemetry reads temp/power/fan on Windows (NVML + ADL)
- [ ] Console banner renders correctly in Windows Terminal
- [ ] Binary runs without Visual C++ Redistributable installed (static CRT)

### Estimated Scope

~10–12 sessions. Higher risk due to cross-platform surface area. CUDA cross-compilation well-documented; AMD OpenCL on Windows is the unknown.

---

## Execution Order & Dependencies

```
Session 50+     Session 54+          Session 58+
    │               │                    │
    ▼               ▼                    ▼
┌─────────┐   ┌───────────┐   ┌──────────────────┐
│ Pillar 1│   │  Pillar 2 │   │    Pillar 3      │
│ Single  │──►│  GUI      │──►│    Windows       │
│ Binary  │   │  Dashboard│   │    Support       │
└─────────┘   └───────────┘   └──────────────────┘
  ~4 sess       ~8-10 sess       ~10-12 sess
```

- **Pillar 1 before Pillar 2:** Embedding web assets requires single-binary infrastructure
- **Pillar 2 before Pillar 3:** Shipping GUI on Windows requires GUI to exist on Linux first
- Pillar 3 can begin socket/platform work in parallel with late Pillar 2 if desired

---

## Release Milestones

| Version | Contents | Pillar |
|---------|----------|--------|
| **v3.2.0** | Single executable (Linux, CUDA + OpenCL) | 1 |
| **v3.3.0** | GUI dashboard (Linux, browser-based) | 2 |
| **v3.4.0** | Windows release (CLI + GUI, CUDA + OpenCL) | 3 |

---

## Files to Create / Modify

### Pillar 1

| Action | Path | Notes |
|--------|------|-------|
| Modify | `CMakeLists.txt` | Static libs, embedded kernels, single binary |
| Delete | `n0s/backend/plugin.hpp` | dlopen abstraction removed |
| Modify | `n0s/backend/backendConnector.cpp` | Direct backend calls |
| Create | `n0s/backend/amd/amd_gpu/opencl/kernels_embedded.hpp` | Embedded .cl sources |
| Modify | `n0s/backend/amd/amd_gpu/gpu.cpp` | Load kernels from embedded strings |
| Modify | `scripts/container-build.sh` | Single-binary output |

### Pillar 2

| Action | Path | Notes |
|--------|------|-------|
| Create | `gui/index.html` | Dashboard SPA |
| Create | `gui/style.css` | Dark theme, RYO branding |
| Create | `gui/app.js` | API client, chart, interactivity |
| Create | `n0s/http/api.cpp` | REST API endpoints (`/api/v1/*`) |
| Create | `n0s/http/api.hpp` | API interface |
| Create | `n0s/http/embedded_assets.hpp` | Gzipped frontend assets (generated) |
| Modify | `n0s/http/httpd.cpp` | Route to API + serve embedded assets |
| Modify | `n0s/misc/executor.cpp` | Hashrate history ring buffer |
| Modify | `n0s/misc/telemetry.hpp` | Expose history data |
| Modify | `n0s/params.hpp` | `--gui`, `--gui-port`, `--gui-dev` flags |
| Modify | `n0s/cli/cli-miner.cpp` | GUI launch logic |
| Create | `CMakeLists.gui.cmake` | Asset embedding build rules |

### Pillar 3

| Action | Path | Notes |
|--------|------|-------|
| Create | `n0s/platform/platform.hpp` | Cross-platform interface |
| Create | `n0s/platform/platform_linux.cpp` | Linux implementations |
| Create | `n0s/platform/platform_windows.cpp` | Windows implementations |
| Modify | `n0s/net/socket.cpp` | Winsock `#ifdef` shims |
| Modify | `n0s/misc/gpu_telemetry.cpp` | NVML + ADL backends |
| Modify | `n0s/misc/console.cpp` | Windows console color support |
| Modify | `n0s/misc/home_dir.hpp` | Windows paths |
| Modify | `CMakeLists.txt` | MSVC support, vcpkg, Windows deps |
| Create | `.github/workflows/build.yml` | CI/CD matrix |
| Create | `scripts/build-windows.ps1` | Windows build helper |

---

## Non-Goals

Things explicitly **not** in scope:

- **Algorithm changes** — CryptoNight-GPU math stays bit-exact
- **Multi-algorithm support** — This is a single-purpose cn-gpu miner
- **Installer / MSI** — Portable single executable, no installation required
- **macOS support** — Metal backend would be a separate future effort
- **Mobile app** — The web dashboard is mobile-responsive for remote monitoring; that's sufficient
- **Electron wrapper** — The browser-based GUI is deliberate. No 200 MB Chromium bundle.
- **Auto-update** — Out of scope. Users download new releases manually.

---

## Success Criteria

| Criteria | Measurement |
|----------|-------------|
| Single file deployment | One executable, zero companion files (Linux + Windows) |
| GUI usability | Pool config + hashrate chart functional from browser |
| Frontend weight | < 50 KB gzipped (embedded in binary) |
| Binary size | < 25 MB (CUDA+OpenCL variant, static linked) |
| Startup time | < 3 seconds to first hash (same as current) |
| Windows parity | Same hashrate as Linux on identical hardware |
| Zero regressions | All existing golden hash tests + live mining pass |
| CLI independence | Every GUI feature also accessible via CLI flags |

---

## Session Notes

### Session 56 (2026-04-03) — Platform Abstraction Layer (Pillar 3.1) 🏗️⚡

**Foundation for Windows support — cross-platform abstraction layer shipped.**

| Component | Detail |
|-----------|--------|
| **`n0s/platform/platform.hpp`** | Cross-platform API: filesystem paths, console, signals, sockets, browser, threads |
| **`n0s/platform/platform_linux.cpp`** | Linux implementations using POSIX APIs (getpwuid, sigaction, fork+exec, termios, pthread) |
| **`n0s/platform/platform_windows.cpp`** | Windows implementations using Win32 APIs (GetEnvironmentVariable, SetConsoleCtrlHandler, ShellExecute, CreateProcess, WSAStartup) |
| **`socks.hpp` refactored** | Dual POSIX/Winsock socket primitives — SOCKET type, sock_close, sock_strerror with `#ifdef _WIN32` |
| **`home_dir.hpp` refactored** | Now delegates to `platform::getHomePath()` — removed `<pwd.h>` / `<unistd.h>` dependencies |
| **`console.cpp` refactored** | Uses `platform::getKey()`, `platform::enableConsoleColors()`, `platform::formatLocalTime()` — removed `<termios.h>` |
| **`executor.cpp` refactored** | Uses `platform::disableSigpipe()` — removed raw `<signal.h>` / `sigaction` |
| **`cli-miner.cpp` refactored** | Uses `platform::openBrowser()` — removed `fork()`/`execlp()`/`<fcntl.h>` |
| **`jpsock.cpp` refactored** | Uses `platform::sockInit()` — removed old `sock_init()` no-op |

**Platform API surface (14 functions):**

| Function | Linux | Windows |
|----------|-------|---------|
| `getHomePath()` | `$HOME` / `getpwuid` | `%USERPROFILE%` |
| `getConfigDir()` | `$XDG_CONFIG_HOME/n0s/` | `%APPDATA%\n0s\` |
| `getCacheDir()` | `$XDG_CACHE_HOME/n0s/` | `%LOCALAPPDATA%\n0s\` |
| `getKey()` | `termios` raw mode | `_getch()` |
| `enableConsoleColors()` | no-op | `ENABLE_VIRTUAL_TERMINAL_PROCESSING` |
| `formatLocalTime()` | `localtime_r` | `localtime_s` |
| `disableSigpipe()` | `sigaction(SIGPIPE, SIG_IGN)` | no-op |
| `installShutdownHandler()` | `sigaction(SIGINT/SIGTERM)` | `SetConsoleCtrlHandler` |
| `openBrowser()` | `fork + execlp("xdg-open")` | `ShellExecuteA("open")` |
| `spawnProcess()` | `fork + execvp` | `CreateProcessA` |
| `setThreadName()` | `pthread_setname_np` | `SetThreadDescription` |
| `sockInit()` / `sockCleanup()` | no-op | `WSAStartup` / `WSACleanup` |
| `platformName()` | `"linux"` | `"windows"` |
| `isWindows()` | `false` | `true` |

**3-GPU validation:**
- nitro (RX 9070 XT, OpenCL): build ✅, 100+ shares, 0 rejected ✅, API endpoints ✅
- nos2 (GTX 1070 Ti, CUDA 11.8): build ✅, 20+ shares, 0 rejected ✅
- nosnode (RTX 2070, CUDA 12.6): build ✅, 20+ shares, 0 rejected ✅
- Container build CUDA 11.8: 3.1 MB ✅, 20+ shares on nos2 ✅
- Golden hash tests: 3/3 pass ✅

**Key learnings:**
- The old `sock_init()` was an inline no-op in `socks.hpp` — when refactored, `jpsock.cpp` still called it, causing an undeclared symbol error. Platform wrapper caught this cleanly.
- `localtime_r` (POSIX) vs `localtime_s` (Windows) have *reversed argument order* — the platform layer hides this.
- Windows `SetThreadDescription` requires converting to `wchar_t` — encapsulated in the platform layer.
- Container build (GCC 11, Ubuntu 22.04) compiles clean with the new platform files — no C++17 issues.
- Zero algorithm changes — all golden hashes identical before and after refactor.

**Next session priorities (Session 57):**
1. **Pillar 3.2: Network Layer (Winsock)** — `#ifdef _WIN32` shims in socket.cpp for `closesocket()`, `WSAStartup` in main()
2. **Pillar 3.3: GPU Telemetry (Windows)** — NVML direct API instead of nvidia-smi subprocess (benefits Linux too)

