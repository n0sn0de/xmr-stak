#pragma once

#ifndef CONF_NO_HTTPD

/*
 * REST API v1 Endpoint Documentation
 *
 * All API handlers are executor member functions (defined in api.cpp)
 * and execute on the executor thread for thread-safe access to miner state.
 *
 * GET endpoints (read-only):
 *   /api/v1/status           — Mining state, uptime, connection status
 *   /api/v1/hashrate         — Per-GPU and total hashrate (10s, 60s, 15m)
 *   /api/v1/hashrate/history — Time-series ring buffer (3600 samples)
 *   /api/v1/gpus             — GPU list with telemetry (temp, power, fan, clocks)
 *   /api/v1/pool             — Current pool, shares, difficulty
 *   /api/v1/config           — Current miner configuration (sanitized)
 *   /api/v1/autotune         — Cached autotune results
 *   /api/v1/version          — Version, build info, backends enabled
 *
 * PUT endpoints (write):
 *   /api/v1/config/pool      — Update pool settings and reconnect
 *     Body: {
 *       "pool_address": "host:port",       (required)
 *       "wallet_address": "...",           (optional, keeps current if empty)
 *       "rig_id": "...",                   (optional)
 *       "pool_password": "...",            (optional, defaults to "x")
 *       "use_tls": true/false,             (optional, defaults to false)
 *       "use_nicehash": true/false         (optional, defaults to false)
 *     }
 *     Response: {"success": true, "pool_address": "...", "message": "..."}
 */

#endif // CONF_NO_HTTPD
