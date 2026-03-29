/**
 * cuda_core.cu — CUDA kernel orchestration for CryptoNight-GPU
 *
 * Contains:
 *   kernel_implode_scratchpad  — Phase 4: AES compression + mix_and_propagate
 *   cryptonight_core_gpu_hash  — Host function that launches all phases
 *   cryptonight_core_cpu_hash  — Entry point called by mining thread
 *
 * Pipeline (all executed on GPU):
 *   Phase 1: cryptonight_extra_gpu_prepare (cuda_extra.cu) — Keccak + AES key setup
 *   Phase 2: kernel_expand_scratchpad (cuda_cryptonight_gpu.hpp) — Keccak-based expansion
 *   Phase 3: kernel_gpu_compute (cuda_cryptonight_gpu.hpp) — FP computation loop
 *   Phase 4: kernel_implode_scratchpad (this file) — AES compression
 *   Phase 5: cryptonight_extra_gpu_final (cuda_extra.cu) — Final Keccak + target check
 */

#include "xmrstak/backend/cryptonight.hpp"

#include <cuda.h>
#include <cuda_runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xmrstak/backend/nvidia/nvcc_code/cuda_cryptonight_gpu.hpp"
#include "xmrstak/jconf.hpp"

#ifdef _WIN32
#include <windows.h>
extern "C" void compat_usleep(uint64_t waitTime)
{
	if(waitTime > 0)
	{
		if(waitTime > 100)
		{
			HANDLE timer;
			LARGE_INTEGER ft;
			ft.QuadPart = -10ll * int64_t(waitTime);
			timer = CreateWaitableTimer(NULL, TRUE, NULL);
			SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
			WaitForSingleObject(timer, INFINITE);
			CloseHandle(timer);
		}
		else
		{
			LARGE_INTEGER perfCnt, start, now;
			__int64 elapsed;
			QueryPerformanceFrequency(&perfCnt);
			QueryPerformanceCounter(&start);
			do
			{
				SwitchToThread();
				QueryPerformanceCounter((LARGE_INTEGER*)&now);
				elapsed = (__int64)((now.QuadPart - start.QuadPart) / (float)perfCnt.QuadPart * 1000 * 1000);
			} while(elapsed < waitTime);
		}
	}
}
#else
#include <unistd.h>
extern "C" void compat_usleep(uint64_t waitTime)
{
	usleep(waitTime);
}
#endif

#include "cuda_context.hpp"
#include "cuda_aes.hpp"
#include "cuda_extra.hpp"

// Index type: 64-bit for large grids on sm_30+
#if defined(XMR_STAK_LARGEGRID) && (__CUDA_ARCH__ >= 300)
typedef uint64_t IndexType;
#else
typedef int IndexType;
#endif

template <typename T>
__forceinline__ __device__ void unusedVar(const T&) {}

/**
 * Warp-level shuffle for data exchange between threads.
 *
 * @tparam group_n  Number of threads in the shuffle group (must be power of 2)
 * @param ptr       Shared memory for pre-sm_30 fallback (NULL for sm_30+)
 * @param sub       Thread index within the group
 * @param val       Value to share
 * @param src       Source thread index to read from
 */
template <size_t group_n>
__forceinline__ __device__ uint32_t shuffle(volatile uint32_t* ptr, const uint32_t sub, const int val, const uint32_t src)
{
#if(__CUDA_ARCH__ < 300)
	ptr[sub] = val;
	return ptr[src & (group_n - 1)];
#else
	unusedVar(ptr);
	unusedVar(sub);
#if(__CUDACC_VER_MAJOR__ >= 9)
	return __shfl_sync(__activemask(), val, src, group_n);
#else
	return __shfl(val, src, group_n);
#endif
#endif
}

// ============================================================
// Phase 4 Kernel: Scratchpad Compression (Implode)
//
// Compresses the 2MB scratchpad back into the 200-byte hash state.
// Uses AES pseudo-rounds with 8-thread shuffle for mix_and_propagate.
//
// cn_gpu mode: Two full passes over the scratchpad.
// Each pass: XOR scratchpad block → AES round → shuffle-XOR (mix)
//
// 8 threads cooperate per hash (each handles 4 bytes of a 32-byte chunk).
// ============================================================

template <xmrstak_algo_id ALGO>
__global__ void kernel_implode_scratchpad(
	const uint32_t ITERATIONS, const size_t MEMORY,
	int threads, int bfactor, int partidx,
	uint32_t* scratchpad_in,
	const uint32_t* const __restrict__ state_buffer_in,
	uint32_t* __restrict__ d_ctx_key2)
{
	__shared__ uint32_t sharedMemoryX[256 * 32];

	// Prevent compiler from folding the shared memory offset into the AES table lookup
	const int twidx = (threadIdx.x * 4) % 128;
	char* sharedMemory = (char*)sharedMemoryX + twidx;

	// Initialize AES lookup tables in shared memory
	cn_aes_gpu_init32(sharedMemoryX);
	__syncthreads();

	// Thread mapping: 8 threads per hash
	const int thread = (blockDim.x * blockIdx.x + threadIdx.x) >> 3;
	const int subv = (threadIdx.x & 7);        // thread index within 8-thread group [0-7]
	const int sub = subv << 2;                  // byte offset within 32-byte chunk [0,4,8,...,28]

	// Work splitting for bfactor
	const int batchsize = MEMORY >> bfactor;
	const int start = (partidx % (1 << bfactor)) * batchsize;
	const int end = start + batchsize;

	if(thread >= threads)
		return;

	// Point to this hash's scratchpad region (uint32 indexed, sub selects 4-byte lane)
	const uint32_t* const scratchpad = scratchpad_in + ((IndexType)thread * MEMORY) + sub;

	// Load AES key (10 rounds × 4 uint32 = 40 uint32)
	uint32_t key[40], text[4];
	#pragma unroll 10
	for(int j = 0; j < 10; ++j)
		((ulonglong4*)key)[j] = ((ulonglong4*)(d_ctx_key2 + thread * 40))[j];

	// Load initial state text from hash_state[64+sub : 64+sub+16]
	uint64_t* d_ctx_state = (uint64_t*)(state_buffer_in + thread * 50 + sub + 16);
	#pragma unroll 2
	for(int j = 0; j < 2; ++j)
		((uint64_t*)text)[j] = loadGlobal64<uint64_t>(d_ctx_state + j);

	__syncthreads();

	// Pre-sm_30 shuffle fallback memory
#if(__CUDA_ARCH__ < 300)
	extern __shared__ uint32_t shuffleMem[];
	volatile uint32_t* sPtr = (volatile uint32_t*)(shuffleMem + (threadIdx.x & 0xFFFFFFF8));
#else
	volatile uint32_t* sPtr = NULL;
#endif

	// Main compression loop: iterate over scratchpad in 32-byte steps
	for(int i = start; i < end; i += 32)
	{
		// XOR scratchpad block into running state
		uint32_t tmp[4];
		((ulonglong2*)(tmp))[0] = ((ulonglong2*)(scratchpad + i))[0];
		#pragma unroll 4
		for(int j = 0; j < 4; ++j)
			text[j] ^= tmp[j];

		// AES pseudo-round (10 rounds with precomputed key)
		((uint4*)text)[0] = cn_aes_pseudo_round_mut32((uint32_t*)sharedMemory, ((uint4*)text)[0], (uint4*)key);

		// mix_and_propagate: XOR with adjacent thread's result (circular shift by 1)
		{
			uint32_t tmp[4];
			#pragma unroll 4
			for(int j = 0; j < 4; ++j)
				tmp[j] = shuffle<8>(sPtr, subv, text[j], (subv + 1) & 7);
			#pragma unroll 4
			for(int j = 0; j < 4; ++j)
				text[j] ^= tmp[j];
		}
	}

	// Write compressed state back to hash_state
	#pragma unroll 2
	for(int j = 0; j < 2; ++j)
		storeGlobal64<uint64_t>(d_ctx_state + j, ((uint64_t*)text)[j]);
}

// ============================================================
// Host: Launch all GPU kernels for CryptoNight-GPU hash
// ============================================================

template <xmrstak_algo_id ALGO, uint32_t MEM_MODE>
void cryptonight_core_gpu_hash(nvid_ctx* ctx, uint32_t nonce, const xmrstak_algo& algo)
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
		xmrstak::nvidia::kernel_expand_scratchpad<<<intensity, 128>>>(
			MEM, (int*)ctx->d_ctx_state, (int*)ctx->d_long_state));

	// ---- Phase 3: GPU floating-point computation loop ----
	// 16 threads per hash, split across bfactor partitions
	const int phase3_partitions = 1 << ctx->device_bfactor;
	for(int i = 0; i < phase3_partitions; i++)
	{
		CUDA_CHECK_KERNEL(
			ctx->device_id,
			xmrstak::nvidia::kernel_gpu_compute<<<
				ctx->device_blocks,
				ctx->device_threads * 16,
				sizeof(xmrstak::nvidia::SharedMemory) * ctx->device_threads>>>(
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
				phase4_block * sizeof(uint32_t) * static_cast<int>(ctx->device_arch[0] < 3)>>>(
				ITERATIONS,
				MEM / 4,
				ctx->device_blocks * ctx->device_threads,
				phase4_bfactor, i,
				ctx->d_long_state,
				ctx->d_ctx_state, ctx->d_ctx_key2));
	}
}

// ============================================================
// Entry point: called by CUDA mining thread
// ============================================================

void cryptonight_core_cpu_hash(nvid_ctx* ctx, const xmrstak_algo& miner_algo, uint32_t startNonce, uint64_t chain_height)
{
	if(miner_algo == invalid_algo)
		return;

	if(ctx->memMode == 1)
		cryptonight_core_gpu_hash<cryptonight_gpu, 1>(ctx, startNonce, miner_algo);
	else
		cryptonight_core_gpu_hash<cryptonight_gpu, 0>(ctx, startNonce, miner_algo);
}
