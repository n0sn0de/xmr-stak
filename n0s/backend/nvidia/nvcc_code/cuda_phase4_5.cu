/**
 * cuda_phase4_5.cu — Phases 4 & 5: Scratchpad compression and finalization
 *
 * Phase 4: Compress the 2MB scratchpad back into the 200-byte hash state
 *          using AES pseudo-rounds with 8-thread shuffle for mix_and_propagate.
 *
 * Phase 5: Finalize hash via 16 rounds of AES + mix_and_propagate + Keccak-f,
 *          then check against difficulty target.
 */

#include "cuda_phase4_5.hpp"
#include "cuda_aes.hpp"
#include "cuda_keccak.hpp"
#include "cuda_extra.hpp"
#include "cuda_cryptonight_gpu.hpp"

#include <cstdint>

// ============================================================
// Index type: always 64-bit (minimum target is sm_60 Pascal)
// ============================================================

using IndexType = uint64_t;

// ============================================================
// Warp-level shuffle helper (sm_60+ always has native shuffle)
// ============================================================

template <size_t group_n>
__forceinline__ __device__ uint32_t shuffle(const int val, const uint32_t src)
{
	return __shfl_sync(__activemask(), val, src, group_n);
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

template <n0s_algo_id ALGO>
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
				tmp[j] = shuffle<8>(text[j], (subv + 1) & 7);
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
// Phase 5 helper: mix_and_propagate (serial, for finalization)
// ============================================================

__device__ __forceinline__ void mix_and_propagate(uint32_t* state)
{
	uint32_t tmp0[4];
	for(size_t x = 0; x < 4; ++x)
		tmp0[x] = (state)[x];

	// set destination [0,6]
	for(size_t t = 0; t < 7; ++t)
		for(size_t x = 0; x < 4; ++x)
			(state + 4 * t)[x] = (state + 4 * t)[x] ^ (state + 4 * (t + 1))[x];

	// set destination 7
	for(size_t x = 0; x < 4; ++x)
		(state + 4 * 7)[x] = (state + 4 * 7)[x] ^ tmp0[x];
}

// ============================================================
// Phase 5 Kernel: Finalize hash and check against target
//
// For each hash:
//   1. Load state from Phase 4 output
//   2. 16 rounds of AES pseudo-round + mix_and_propagate
//   3. Final Keccak-f permutation
//   4. Compare hash against difficulty target
//   5. If below target, atomically store nonce in result buffer
//
// cn_gpu outputs directly: no extra_hashes branch (blake/groestl/jh/skein).
// ============================================================

template <n0s_algo_id ALGO>
__global__ void cryptonight_extra_gpu_final(int threads, uint64_t target, uint32_t* __restrict__ d_res_count, uint32_t* __restrict__ d_res_nonce, uint32_t* __restrict__ d_ctx_state, uint32_t* __restrict__ d_ctx_key2)
{
	const int thread = blockDim.x * blockIdx.x + threadIdx.x;

	__shared__ uint32_t sharedMemory[1024];

	cn_aes_gpu_init(sharedMemory);
	__syncthreads();

	if(thread >= threads)
		return;

	int i;
	uint32_t* __restrict__ ctx_state = d_ctx_state + thread * 50;
	uint32_t state[50];

#pragma unroll
	for(i = 0; i < 50; i++)
		state[i] = ctx_state[i];

	// cn_gpu requires 16 rounds of AES + mix_and_propagate before final keccak
	uint32_t key[40];
	MEMCPY8(key, d_ctx_key2 + thread * 40, 20);

	for(int i = 0; i < 16; i++)
	{
		for(size_t t = 4; t < 12; ++t)
		{
			cn_aes_pseudo_round_mut(sharedMemory, state + 4u * t, key);
		}
		mix_and_propagate(state + 4 * 4);
	}

	cn_keccakf2((uint64_t*)state);

	// cn_gpu outputs directly (no branch dispatcher)
	if(((uint64_t*)state)[3] < target)
	{
		uint32_t idx = atomicInc(d_res_count, 0xFFFFFFFF);

		if(idx < 10)
			d_res_nonce[idx] = thread;
	}
}

// Explicit template instantiations (for cryptonight_gpu)
template __global__ void kernel_implode_scratchpad<cryptonight_gpu>(
	const uint32_t ITERATIONS,
	const size_t MEMORY,
	int threads,
	int bfactor,
	int partidx,
	uint32_t* scratchpad_in,
	const uint32_t* const __restrict__ state_buffer_in,
	uint32_t* __restrict__ d_ctx_key2);

template __global__ void cryptonight_extra_gpu_final<cryptonight_gpu>(
	int threads,
	uint64_t target,
	uint32_t* __restrict__ d_res_count,
	uint32_t* __restrict__ d_res_nonce,
	uint32_t* __restrict__ d_ctx_state,
	uint32_t* __restrict__ d_ctx_key2);
