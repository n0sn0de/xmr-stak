/**
 * cuda_dispatch.hpp — CUDA kernel dispatch and host-side entry points
 *
 * Orchestrates the complete CryptoNight-GPU pipeline on NVIDIA GPUs.
 */
#pragma once

#include "cuda_context.hpp"
#include "n0s/backend/cryptonight.hpp"
#include "n0s/backend/kernel_profile.hpp"

// C++ linkage functions (take C++ types like n0s_algo&)

/** Set input data for hashing (POW block header)
 *
 * @param ctx NVIDIA context
 * @param data Input blob (POW block header)
 * @param len Length of input in bytes
 */
void cryptonight_extra_cpu_set_data(nvid_ctx* ctx, const void* data, uint32_t len);

/** Launch Phase 1: Prepare hash state via Keccak + AES key expansion
 *
 * @param ctx NVIDIA context
 * @param startNonce Starting nonce for this batch
 * @param miner_algo Algorithm parameters
 */
void cryptonight_extra_cpu_prepare(nvid_ctx* ctx, uint32_t startNonce, const n0s_algo& miner_algo);

/** Launch Phase 5: Finalize hash and check against target
 *
 * @param ctx NVIDIA context
 * @param startNonce Starting nonce for this batch
 * @param target Difficulty target
 * @param rescount Output: number of valid nonces found
 * @param resnonce Output: array of valid nonces (up to 10)
 * @param miner_algo Algorithm parameters
 */
void cryptonight_extra_cpu_final(nvid_ctx* ctx, uint32_t startNonce, uint64_t target, uint32_t* rescount, uint32_t* resnonce, const n0s_algo& miner_algo);

/** Entry point: Launch full CryptoNight-GPU hash pipeline
 *
 * Orchestrates Phases 2-4 (Phase 1 and 5 are launched separately).
 *
 * @param ctx NVIDIA context
 * @param miner_algo Algorithm parameters
 * @param startNonce Starting nonce for this batch
 * @param chain_height Blockchain height (unused by cn_gpu)
 */
void cryptonight_core_cpu_hash(nvid_ctx* ctx, const n0s_algo& miner_algo, uint32_t startNonce, uint64_t chain_height);

/** Launch full CryptoNight-GPU hash pipeline with per-phase timing
 *
 * Uses cudaEvent_t for GPU-accurate timing between phases.
 * Results accumulated into profile struct for averaging.
 */
void cryptonight_core_cpu_hash_profile(nvid_ctx* ctx, const n0s_algo& miner_algo, uint32_t startNonce, uint64_t chain_height, n0s::KernelProfile& profile);

#ifdef __cplusplus
extern "C" {
#endif

/** usleep wrapper (extern "C" for ABI compatibility) */
void compat_usleep(uint64_t waitTime);

#ifdef __cplusplus
}
#endif
