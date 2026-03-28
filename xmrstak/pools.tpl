R"===(// n0s-cngpu pool configuration
// Algorithm: CryptoNight-GPU (dedicated miner)

/*
 * pool_address    - Pool address as "host:port" (e.g. "pool.ryo-currency.com:3333"). Stratum only.
 * wallet_address  - Your wallet address or pool login.
 * rig_id          - Rig identifier for pool-side statistics (needs pool support). Can be empty.
 * pool_password   - Pool password. Usually empty or "x".
 * use_nicehash    - Limit nonce to 3 bytes (NiceHash compatibility).
 * use_tls         - Connect using TLS/SSL.
 * tls_fingerprint - Server's SHA256 fingerprint for cert pinning. Leave empty if unused.
 * pool_weight     - Pool priority weight. Higher = preferred. Must be > 0.
 */

"pool_list" :
[
POOLCONF],

// n0s-cngpu only supports the cryptonight_gpu algorithm.
// Valid values: "cryptonight_gpu" or "ryo"
"currency" : "CURRENCY",
)==="
