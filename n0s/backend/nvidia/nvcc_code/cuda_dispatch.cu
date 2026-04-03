/**
 * cuda_dispatch.cu — CUDA kernel dispatch and host-side entry points
 *
 * Orchestrates the complete CryptoNight-GPU pipeline:
 *   Phase 1: cryptonight_extra_cpu_prepare  — Keccak + AES key expansion
 *   Phase 2: kernel_expand_scratchpad       — Keccak-based scratchpad fill
 *   Phase 3: kernel_gpu_compute             — FP computation loop
 *   Phase 4: kernel_implode_scratchpad      — AES compression + mix_and_propagate
 *   Phase 5: cryptonight_extra_cpu_final    — Final Keccak + target check
 */

#include "cuda_dispatch.hpp"
#include "cuda_device.hpp"
#include "cuda_phase1.hpp"
#include "cuda_phase4_5.hpp"
#include "cuda_cryptonight_gpu.hpp"
#include "cuda_extra.hpp"

#include "n0s/backend/cryptonight.hpp"
#include "n0s/jconf.hpp"

#include <cuda.h>
#include <cuda_runtime.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================
// usleep wrapper (extern "C" for ABI compatibility)
// ============================================================

extern "C" void compat_usleep(uint64_t waitTime)
{
#ifdef _WIN32
	// Windows Sleep() takes milliseconds; usleep takes microseconds
	if(waitTime >= 1000)
		Sleep(static_cast<DWORD>(waitTime / 1000));
	else if(waitTime > 0)
		Sleep(1);
#else
	usleep(waitTime);
#endif
}

// ============================================================
// Host: Launch all GPU kernels for CryptoNight-GPU hash
//
// Orchestrates phases 2-4 (phase 1 and 5 are launched separately
// by cryptonight_extra_cpu_prepare and cryptonight_extra_cpu_final).
// ============================================================

template <n0s_algo_id ALGO, uint32_t MEM_MODE>
void cryptonight_core_gpu_hash(nvid_ctx* ctx, uint32_t nonce, const n0s_algo& algo)
{
	const uint32_t MASK = algo.Mask();
	const uint32_t ITERATIONS = algo.Iter();
	const size_t MEM = algo.Mem();

	dim3 grid(ctx->device_blocks);
	dim3 block(ctx->device_threads);
	dim3 block8(ctx->device_threads << 3);   // 8 threads per hash for phase 4

	const size_t intensity = ctx->device_blocks * ctx->device_threads;

	// ---- Phase 2: Expand scratchpad (keccak-based) ----
	CUDA_CHECK_KERNEL(
		ctx->device_id,
		n0s::cuda::kernel_expand_scratchpad<<<intensity, 128>>>(
			MEM, (int*)ctx->d_ctx_state, (int*)ctx->d_long_state));

	// ---- Phase 3: GPU floating-point computation loop ----
	// 16 threads per hash, split across bfactor partitions
	const int phase3_partitions = 1 << ctx->device_bfactor;
	for(int i = 0; i < phase3_partitions; i++)
	{
		CUDA_CHECK_KERNEL(
			ctx->device_id,
			n0s::cuda::kernel_gpu_compute<<<
				ctx->device_blocks,
				ctx->device_threads * 16,
				sizeof(n0s::cuda::SharedMemory) * ctx->device_threads>>>(
				ITERATIONS, MEM, MASK,
				(int*)ctx->d_ctx_state,
				(int*)ctx->d_long_state,
				ctx->device_bfactor, i,
				ctx->d_ctx_a, ctx->d_ctx_b));
	}

	// ---- Phase 4: Implode scratchpad (AES + mix_and_propagate) ----
	// Less work than phase 3, so only split at bfactor >= 8
	int phase4_bfactor = ctx->device_bfactor - 8;
	if(phase4_bfactor < 0)
		phase4_bfactor = 0;

	// cn_gpu: two full passes over scratchpad (HEAVY_MIX mode)
	const int phase4_partitions = (1 << phase4_bfactor) * 2;

	int phase4_block = block8.x;
	int phase4_grid = grid.x;
	// Double threads per block if hardware allows (improves occupancy)
	if(phase4_block * 2 <= ctx->device_maxThreadsPerBlock)
	{
		phase4_block *= 2;
		phase4_grid = (phase4_grid + 1) / 2;
	}

	for(int i = 0; i < phase4_partitions; i++)
	{
		CUDA_CHECK_KERNEL(ctx->device_id,
			kernel_implode_scratchpad<ALGO><<<
				phase4_grid,
				phase4_block,
				0>>>(  // No dynamic shared memory needed (sm_60+ has native shuffle)
				ITERATIONS,
				MEM / 4,
				ctx->device_blocks * ctx->device_threads,
				phase4_bfactor, i,
				ctx->d_long_state,
				ctx->d_ctx_state, ctx->d_ctx_key2));
	}
}

// ============================================================
// Entry point (profiling): called by CUDA mining thread with --profile
//
// Uses cudaEvent_t for GPU-accurate per-phase timing.
// ============================================================

template <n0s_algo_id ALGO, uint32_t MEM_MODE>
void cryptonight_core_gpu_hash_profile(nvid_ctx* ctx, uint32_t nonce, const n0s_algo& algo, n0s::KernelProfile& profile)
{
	const uint32_t MASK = algo.Mask();
	const uint32_t ITERATIONS = algo.Iter();
	const size_t MEM = algo.Mem();

	dim3 grid(ctx->device_blocks);
	dim3 block(ctx->device_threads);
	dim3 block8(ctx->device_threads << 3);

	const size_t intensity = ctx->device_blocks * ctx->device_threads;

	// Create CUDA events for timing
	cudaEvent_t ev_start, ev_p2, ev_p3, ev_p4, ev_p5, ev_end;
	cudaEventCreate(&ev_start);
	cudaEventCreate(&ev_p2);
	cudaEventCreate(&ev_p3);
	cudaEventCreate(&ev_p4);
	cudaEventCreate(&ev_p5);
	cudaEventCreate(&ev_end);

	// Phase 1 timing is done externally (prepare is a separate call)
	// Record start before Phase 2
	cudaEventRecord(ev_start);

	// ---- Phase 2: Expand scratchpad ----
	CUDA_CHECK_KERNEL(
		ctx->device_id,
		n0s::cuda::kernel_expand_scratchpad<<<intensity, 128>>>(
			MEM, (int*)ctx->d_ctx_state, (int*)ctx->d_long_state));

	cudaEventRecord(ev_p2);

	// ---- Phase 3: GPU floating-point computation loop ----
	const int phase3_partitions = 1 << ctx->device_bfactor;
	for(int i = 0; i < phase3_partitions; i++)
	{
		CUDA_CHECK_KERNEL(
			ctx->device_id,
			n0s::cuda::kernel_gpu_compute<<<
				ctx->device_blocks,
				ctx->device_threads * 16,
				sizeof(n0s::cuda::SharedMemory) * ctx->device_threads>>>(
				ITERATIONS, MEM, MASK,
				(int*)ctx->d_ctx_state,
				(int*)ctx->d_long_state,
				ctx->device_bfactor, i,
				ctx->d_ctx_a, ctx->d_ctx_b));
	}

	cudaEventRecord(ev_p3);

	// ---- Phase 4: Implode scratchpad ----
	int phase4_bfactor = ctx->device_bfactor - 8;
	if(phase4_bfactor < 0)
		phase4_bfactor = 0;

	const int phase4_partitions = (1 << phase4_bfactor) * 2;

	int phase4_block = block8.x;
	int phase4_grid = grid.x;
	if(phase4_block * 2 <= ctx->device_maxThreadsPerBlock)
	{
		phase4_block *= 2;
		phase4_grid = (phase4_grid + 1) / 2;
	}

	for(int i = 0; i < phase4_partitions; i++)
	{
		CUDA_CHECK_KERNEL(ctx->device_id,
			kernel_implode_scratchpad<ALGO><<<
				phase4_grid,
				phase4_block,
				0>>>(  // No dynamic shared memory needed (sm_60+ has native shuffle)
				ITERATIONS,
				MEM / 4,
				ctx->device_blocks * ctx->device_threads,
				phase4_bfactor, i,
				ctx->d_long_state,
				ctx->d_ctx_state, ctx->d_ctx_key2));
	}

	cudaEventRecord(ev_p4);

	// ---- Phase 5: Finalize (launched separately by caller, but we time it here) ----
	// Note: Phase 5 is launched by cryptonight_extra_cpu_final() which is called
	// after this function returns. So ev_p4→ev_end only captures Phase 4.
	// We record ev_end = ev_p4 for backward compat, and let the caller time Phase 5.

	cudaEventRecord(ev_end);
	cudaEventSynchronize(ev_end);

	// Read timing
	float ms_p2 = 0, ms_p3 = 0, ms_p4 = 0;
	cudaEventElapsedTime(&ms_p2, ev_start, ev_p2);
	cudaEventElapsedTime(&ms_p3, ev_p2, ev_p3);
	cudaEventElapsedTime(&ms_p4, ev_p3, ev_p4);

	profile.phase2_us += static_cast<int64_t>(ms_p2 * 1000.0f);
	profile.phase3_us += static_cast<int64_t>(ms_p3 * 1000.0f);
	profile.phase4_us += static_cast<int64_t>(ms_p4 * 1000.0f);
	profile.phase45_us += static_cast<int64_t>(ms_p4 * 1000.0f);  // Phase 5 added by caller
	profile.total_us += static_cast<int64_t>((ms_p2 + ms_p3 + ms_p4) * 1000.0f);
	profile.iterations++;

	cudaEventDestroy(ev_start);
	cudaEventDestroy(ev_p2);
	cudaEventDestroy(ev_p3);
	cudaEventDestroy(ev_p4);
	cudaEventDestroy(ev_p5);
	cudaEventDestroy(ev_end);
}

void cryptonight_core_cpu_hash_profile(nvid_ctx* ctx, const n0s_algo& miner_algo, uint32_t startNonce, uint64_t chain_height, n0s::KernelProfile& profile)
{
	if(miner_algo == invalid_algo)
		return;

	if(ctx->memMode == 1)
		cryptonight_core_gpu_hash_profile<cryptonight_gpu, 1>(ctx, startNonce, miner_algo, profile);
	else
		cryptonight_core_gpu_hash_profile<cryptonight_gpu, 0>(ctx, startNonce, miner_algo, profile);
}

// ============================================================
// Entry point: called by CUDA mining thread
// ============================================================

void cryptonight_core_cpu_hash(nvid_ctx* ctx, const n0s_algo& miner_algo, uint32_t startNonce, uint64_t chain_height)
{
	if(miner_algo == invalid_algo)
		return;

	if(ctx->memMode == 1)
		cryptonight_core_gpu_hash<cryptonight_gpu, 1>(ctx, startNonce, miner_algo);
	else
		cryptonight_core_gpu_hash<cryptonight_gpu, 0>(ctx, startNonce, miner_algo);
}

// ============================================================
// Host: Phase 1 launch (prepare) and Phase 5 launch (finalize)
// ============================================================

extern "C" void cryptonight_extra_cpu_set_data(nvid_ctx* ctx, const void* data, uint32_t len)
{
	ctx->inputlen = len;
	CUDA_CHECK(ctx->device_id, cudaMemcpy(ctx->d_input, data, len, cudaMemcpyHostToDevice));
}

extern "C" void cryptonight_extra_cpu_prepare(nvid_ctx* ctx, uint32_t startNonce, const n0s_algo& miner_algo)
{
	int threadsperblock = 128;
	uint32_t wsize = ctx->device_blocks * ctx->device_threads;

	dim3 grid((wsize + threadsperblock - 1) / threadsperblock);
	dim3 block(threadsperblock);

	CUDA_CHECK_KERNEL(ctx->device_id, cryptonight_extra_gpu_prepare<cryptonight_gpu><<<grid, block>>>(wsize, ctx->d_input, ctx->inputlen, startNonce,
										  ctx->d_ctx_state, ctx->d_ctx_state2, ctx->d_ctx_a, ctx->d_ctx_b, ctx->d_ctx_key1, ctx->d_ctx_key2));
}

extern "C" void cryptonight_extra_cpu_final(nvid_ctx* ctx, uint32_t startNonce, uint64_t target, uint32_t* rescount, uint32_t* resnonce, const n0s_algo& miner_algo)
{
	int threadsperblock = 128;
	uint32_t wsize = ctx->device_blocks * ctx->device_threads;

	dim3 grid((wsize + threadsperblock - 1) / threadsperblock);
	dim3 block(threadsperblock);

	CUDA_CHECK(ctx->device_id, cudaMemset(ctx->d_result_nonce, 0xFF, 10 * sizeof(uint32_t)));
	CUDA_CHECK(ctx->device_id, cudaMemset(ctx->d_result_count, 0, sizeof(uint32_t)));

	CUDA_CHECK_MSG_KERNEL(
		ctx->device_id,
		"\n**suggestion: Try to increase the value of the attribute 'bfactor' in the NVIDIA config file.**",
		cryptonight_extra_gpu_final<cryptonight_gpu><<<grid, block>>>(wsize, target, ctx->d_result_count, ctx->d_result_nonce, ctx->d_ctx_state, ctx->d_ctx_key2));

	CUDA_CHECK(ctx->device_id, cudaMemcpy(rescount, ctx->d_result_count, sizeof(uint32_t), cudaMemcpyDeviceToHost));
	CUDA_CHECK_MSG(
		ctx->device_id,
		"\n**suggestion: Try to increase the attribute 'bfactor' in the NVIDIA config file.**",
		cudaMemcpy(resnonce, ctx->d_result_nonce, 10 * sizeof(uint32_t), cudaMemcpyDeviceToHost));

	/* There is only a 32bit limit for the counter on the device side
	 * therefore this value can be greater than 10, in that case limit rescount
	 * to 10 entries.
	 */
	if(*rescount > 10)
		*rescount = 10;
	for(int i = 0; i < *rescount; i++)
		resnonce[i] += startNonce;
}
