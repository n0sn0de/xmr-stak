#pragma once

#ifndef CONF_NO_HTTPD

#include <cstdint>
#include <string>

namespace n0s
{
namespace api
{

/// REST API endpoint handlers
/// All return JSON strings ready to serve via microhttpd

/// GET /api/v1/status — Mining state, uptime, connection status
std::string status();

/// GET /api/v1/hashrate — Per-GPU and total hashrate (10s, 60s, 15m windows)
std::string hashrate();

/// GET /api/v1/gpus — GPU list with telemetry (temp, power, fan, clocks)
std::string gpus();

/// GET /api/v1/pool — Current pool, accepted/rejected shares, difficulty
std::string pool();

/// GET /api/v1/version — Version, build info, backends enabled
std::string version();

} // namespace api
} // namespace n0s

#endif // CONF_NO_HTTPD
