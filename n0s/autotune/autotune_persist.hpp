#pragma once

#include "autotune_types.hpp"

#include <string>

namespace n0s
{
namespace autotune
{

/// Save autotune results to a JSON file
/// Returns true on success
bool saveAutotuneResult(const AutotuneResult& result, const std::string& filepath);

/// Load autotune results from a JSON file
/// Returns true on success, fills result
bool loadAutotuneResult(AutotuneResult& result, const std::string& filepath);

/// Find a cached autotune state for a device by fingerprint compatibility
/// Returns pointer to matching state, or nullptr if none found
const AutotuneState* findCachedState(const AutotuneResult& result, const DeviceFingerprint& fingerprint);

} // namespace autotune
} // namespace n0s
