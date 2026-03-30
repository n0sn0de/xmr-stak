/**
 * cuda_phase2_3.cu — CryptoNight-GPU Phase 2 & 3 CUDA Kernels
 *
 * Phase 2: Scratchpad Expansion (Keccak-based)
 * Phase 3: GPU Floating-Point Computation Loop
 *
 * These kernels are the core of the CryptoNight-GPU algorithm.
 *
 * CRITICAL: NVCC must be invoked with --fmad=false --prec-div=true --ftz=false
 * to ensure IEEE 754 compliance for bit-exact hashes.
 *
 * Original: xmr-stak by fireice-uk & psychocrypt
 * Cleaned up by n0sn0de
 */

#include "cuda_cryptonight_gpu.hpp"
#include "cuda_keccak.hpp"
#include "cuda_extra.hpp"

namespace n0s
{
namespace cuda
{

// ============================================================
// Phase 2: Scratchpad Expansion
//
// Expands the 200-byte Keccak state into the 2MB scratchpad
// using repeated Keccak-f permutations. Each 512-byte chunk
// is generated from (state XOR chunk_index).
// ============================================================

/// Generate one 512-byte scratchpad chunk from Keccak state
__forceinline__ __device__ void generate_512_bytes(uint64_t idx, const uint64_t* in, uint8_t* out)
{
	uint64_t hash[25];

	hash[0] = in[0] ^ idx;
#pragma unroll 24
	for(int i = 1; i < 25; ++i)
		hash[i] = in[i];

	cn_keccakf2(hash);
#pragma unroll 10
	for(int i = 0; i < 10; ++i)
		((ulonglong2*)out)[i] = ((ulonglong2*)hash)[i];
	out += 160;

	cn_keccakf2(hash);
#pragma unroll 11
	for(int i = 0; i < 11; ++i)
		((ulonglong2*)out)[i] = ((ulonglong2*)hash)[i];
	out += 176;

	cn_keccakf2(hash);
#pragma unroll 11
	for(int i = 0; i < 11; ++i)
		((ulonglong2*)out)[i] = ((ulonglong2*)hash)[i];
}

/// Phase 2 Kernel: Expand 200-byte state into 2MB scratchpad
__global__ void kernel_expand_scratchpad(const size_t MEMORY, int32_t* state_buffer_in, int* scratchpad_in)
{
	__shared__ uint64_t state[25];

	uint8_t* scratchpad = (uint8_t*)scratchpad_in + blockIdx.x * MEMORY;
	uint64_t* state_ptr = (uint64_t*)((uint8_t*)state_buffer_in + blockIdx.x * 200);

	// Load 200-byte state into shared memory
	for(int i = threadIdx.x; i < 25; i += blockDim.x)
		state[i] = loadGlobal64<uint64_t>(state_ptr + i);

	if(blockDim.x > 32)
		__syncthreads();
	else
		warp_sync();

	// Each thread generates one or more 512-byte chunks
	for(uint64_t i = threadIdx.x; i < MEMORY / 512; i += blockDim.x)
	{
		generate_512_bytes(i, state, (uint8_t*)scratchpad + i * 512);
	}
}

// ============================================================
// Phase 3 Kernel: GPU Floating-Point Computation
//
// This is the core of CryptoNight-GPU. Each hash requires 16 threads
// cooperating via shared memory. Each iteration:
//   1. Load 64 bytes from scratchpad (4 threads × 16 bytes)
//   2. Each thread computes FP chain using shuffled data from other threads
//   3. XOR results back into scratchpad
//   4. Reduce accumulators to compute next scratchpad address
//
// bfactor splits the work across multiple kernel launches to avoid
// GPU watchdog timeouts on desktop systems.
// ============================================================

__launch_bounds__(128, 8)
__global__ void kernel_gpu_compute(
	const uint32_t ITERATIONS, const size_t MEMORY, const uint32_t MASK,
	int32_t* state_buffer, int* scratchpad_in,
	int bfactor, int partidx,
	uint32_t* roundVs, uint32_t* roundS)
{
	const int batchsize = (ITERATIONS * 2) >> (1 + bfactor);

	extern __shared__ SharedMemory smemExtern_in[];

	const uint32_t chunk = threadIdx.x / 16;
	const uint32_t numHashPerBlock = blockDim.x / 16;

	// Each hash gets its own scratchpad region in global memory
	int* scratchpad = (int*)((uint8_t*)scratchpad_in + size_t(MEMORY) * (blockIdx.x * numHashPerBlock + chunk));

	SharedMemory* smem = smemExtern_in + chunk;

	const uint32_t tid = threadIdx.x % 16;
	const uint32_t idxHash = blockIdx.x * numHashPerBlock + threadIdx.x / 16;

	// Initial scratchpad index from hash state
	uint32_t s = 0;

	// Floating-point accumulator — carries state between iterations
	__m128 fp_accumulator(0);
	if(partidx != 0)
	{
		// Resume from previous bfactor partition
		fp_accumulator = ((__m128*)roundVs)[idxHash];
		s = roundS[idxHash];
	}
	else
	{
		// First partition: seed from hash state byte 1-3
		s = ((uint32_t*)state_buffer)[idxHash * 50] >> 8;
	}

	// group_index: which 4-thread group (0-3) within the 16-thread hash team
	// lane_index: position within the group (0-3)
	const uint32_t group_index = tid / 4;
	const uint32_t lane_index = tid % 4;
	const uint32_t block = group_index * 16 + lane_index;

	for(int i = 0; i < batchsize; i++)
	{
		// Step 1: Load 64 bytes from scratchpad into shared memory
		warp_sync();
		int tmp = loadGlobal32<int>(((int*)scratchpad_ptr(s, group_index, scratchpad, MASK)) + lane_index);
		((int*)smem->computation_output)[tid] = tmp;
		warp_sync();

		// Step 2: Compute FP chain using cross-thread shuffled data
		__m128 rc = fp_accumulator;
		compute_fp_chain_rotated(
			lane_index,
			*(smem->computation_output + SHUFFLE_PATTERN[tid][0]),
			*(smem->computation_output + SHUFFLE_PATTERN[tid][1]),
			*(smem->computation_output + SHUFFLE_PATTERN[tid][2]),
			*(smem->computation_output + SHUFFLE_PATTERN[tid][3]),
			THREAD_CONSTANTS[tid], rc, smem->fp_accumulators[tid],
			smem->computation_output[tid]);

		warp_sync();

		// Step 3: XOR-reduce within group and write back to scratchpad
		int outXor = ((int*)smem->computation_output)[block];
		for(uint32_t dd = block + 4; dd < (group_index + 1) * 16; dd += 4)
			outXor ^= ((int*)smem->computation_output)[dd];

		storeGlobal32(((int*)scratchpad_ptr(s, group_index, scratchpad, MASK)) + lane_index, outXor ^ tmp);
		((int*)smem->computation_output)[tid] = outXor;

		// Step 4: Reduce float accumulators within groups
		float va_tmp1 = ((float*)smem->fp_accumulators)[block] + ((float*)smem->fp_accumulators)[block + 4];
		float va_tmp2 = ((float*)smem->fp_accumulators)[block + 8] + ((float*)smem->fp_accumulators)[block + 12];
		((float*)smem->fp_accumulators)[tid] = va_tmp1 + va_tmp2;

		warp_sync();

		// Step 5: Cross-group XOR and final accumulator reduction
		__m128i out2 = smem->computation_output[0] ^ smem->computation_output[1]
					 ^ smem->computation_output[2] ^ smem->computation_output[3];

		va_tmp1 = ((float*)smem->fp_accumulators)[block] + ((float*)smem->fp_accumulators)[block + 4];
		va_tmp2 = ((float*)smem->fp_accumulators)[block + 8] + ((float*)smem->fp_accumulators)[block + 12];
		((float*)smem->fp_accumulators)[tid] = va_tmp1 + va_tmp2;

		warp_sync();

		// Step 6: Compute next scratchpad address
		fp_accumulator = smem->fp_accumulators[0];
		fp_accumulator.abs();

		// Scale accumulator to integer range and XOR with output
		auto xx = _mm_mul_ps(fp_accumulator, __m128(16777216.0f));  // FP_NORMALIZE_SCALE
		auto xx_int = xx.get_int();
		out2 = _mm_xor_si128(xx_int, out2);

		// Normalize accumulator to [0, 1) for next iteration
		fp_accumulator = _mm_div_ps(fp_accumulator, __m128(64.0f));  // FP_RANGE_DIVISOR

		// New scratchpad index = XOR of all 4 output words
		s = out2.x ^ out2.y ^ out2.z ^ out2.w;
	}

	// Save state for next bfactor partition (only thread 0 of each hash)
	if(partidx != ((1 << bfactor) - 1) && threadIdx.x % 16 == 0)
	{
		const uint32_t numHashPerBlock2 = blockDim.x / 16;
		const uint32_t idxHash2 = blockIdx.x * numHashPerBlock2 + threadIdx.x / 16;
		((__m128*)roundVs)[idxHash2] = fp_accumulator;
		roundS[idxHash2] = s;
	}
}

} // namespace cuda
} // namespace n0s
