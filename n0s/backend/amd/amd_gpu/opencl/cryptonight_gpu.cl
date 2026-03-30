R"===(
/*
 * CryptoNight-GPU OpenCL Kernels — Phase 3 Floating-Point Computation
 *
 * This file implements the GPU-heavy Phase 3 of CryptoNight-GPU.
 * 16 threads cooperate per hash via local memory to compute the
 * floating-point chain that dominates mining time.
 *
 * See docs/CN-GPU-WHITEPAPER.md for algorithm details.
 * See n0s/algorithm/cn_gpu.hpp for constant definitions.
 *
 * CRITICAL: OpenCL must be configured with -cl-fp32-correctly-rounded-divide-sqrt
 * and IEEE 754 compliance for bit-exact hashes.
 */

/* Scratchpad addressing: get pointer to 16-byte chunk at 64-byte aligned slot */
inline global int4* scratchpad_ptr(uint idx, uint n, __global int *scratchpad) { return (__global int4*)((__global char*)scratchpad + (idx & MASK) + n * 16); }

/* Break FMA dependency chain by forcing exponent to ?????01 */
inline float4 fma_break(float4 x)
{
	x = _mm_and_ps(x, 0xFEFFFFFF);
	return _mm_or_ps(x, 0x00800000);
}

/*
 * fp_sub_round — One sub-round of the floating-point computation
 *
 * Computes numerator (n) and denominator (d) contributions from 4 input vectors,
 * with constant feedback to maintain the dependency chain.
 */
inline void fp_sub_round(float4 n0, float4 n1, float4 n2, float4 n3, float4 rnd_c, float4* n, float4* d, float4* c)
{
	/* Numerator contribution */
	n1 = _mm_add_ps(n1, *c);
	float4 nn = _mm_mul_ps(n0, *c);
	nn = _mm_mul_ps(n1, _mm_mul_ps(nn,nn));
	nn = fma_break(nn);
	*n = _mm_add_ps(*n, nn);

	/* Denominator contribution */
	n3 = _mm_sub_ps(n3, *c);
	float4 dd = _mm_mul_ps(n2, *c);
	dd = _mm_mul_ps(n3, _mm_mul_ps(dd,dd));
	dd = fma_break(dd);
	*d = _mm_add_ps(*d, dd);

	/* Constant feedback: drift accumulator to prevent convergence */
	*c = _mm_add_ps(*c, rnd_c);
	*c = _mm_add_ps(*c, (float4)(0.734375f));
	float4 r = _mm_add_ps(nn, dd);
	r = _mm_and_ps(r, 0x807FFFFF);
	r = _mm_or_ps(r, 0x40000000);
	*c = _mm_add_ps(*c, r);
}

/*
 * fp_round — One full round: 8 sub-rounds with rotated inputs, then safe division
 */
inline void fp_round(float4 n0, float4 n1, float4 n2, float4 n3, float4 rnd_c, float4* c, float4* r)
{
	float4 n = (float4)(0.0f);
	float4 d = (float4)(0.0f);

	fp_sub_round(n0, n1, n2, n3, rnd_c, &n, &d, c);
	fp_sub_round(n1, n2, n3, n0, rnd_c, &n, &d, c);
	fp_sub_round(n2, n3, n0, n1, rnd_c, &n, &d, c);
	fp_sub_round(n3, n0, n1, n2, rnd_c, &n, &d, c);
	fp_sub_round(n3, n2, n1, n0, rnd_c, &n, &d, c);
	fp_sub_round(n2, n1, n0, n3, rnd_c, &n, &d, c);
	fp_sub_round(n1, n0, n3, n2, rnd_c, &n, &d, c);
	fp_sub_round(n0, n3, n2, n1, rnd_c, &n, &d, c);

	/* Clamp denominator: |d| > 2.0 to prevent division hazards */
	d = _mm_and_ps(d, 0xFF7FFFFF);
	d = _mm_or_ps(d, 0x40000000);
	*r =_mm_add_ps(*r, _mm_div_ps(n,d));
}

/*
 * compute_fp_chain — Full FP computation for one thread
 * Returns integer result for scratchpad XOR.
 */
inline int4 compute_fp_chain(float4 n0, float4 n1, float4 n2, float4 n3, float cnt, float4 rnd_c, __local float4* sum)
{
	float4 c= (float4)(cnt);
	float4 r = (float4)(0.0f);

	for(int i = 0; i < 4; ++i)
		fp_round(n0, n1, n2, n3, rnd_c, &c, &r);

	/* Quick fmod: force result into [2.0, 4.0) range */
	r = _mm_and_ps(r, 0x807FFFFF);
	r = _mm_or_ps(r, 0x40000000);
	*sum = r;
	float4 x = (float4)(536870880.0f);  /* FP_RESULT_SCALE */
	r = _mm_mul_ps(r, x);
	return convert_int4_rte(r);
}

/*
 * compute_fp_chain_rotated — Wrapper applying byte rotation to result
 */
inline void compute_fp_chain_rotated(const uint rot, int4 v0, int4 v1, int4 v2, int4 v3, float cnt, float4 rnd_c, __local float4* sum, __local int4* out)
{
	float4 n0 = convert_float4_rte(v0);
	float4 n1 = convert_float4_rte(v1);
	float4 n2 = convert_float4_rte(v2);
	float4 n3 = convert_float4_rte(v3);

	int4 r = compute_fp_chain(n0, n1, n2, n3, cnt, rnd_c, sum);
	*out = rot == 0 ? r : _mm_alignr_epi8(r, rot);
}

)==="
	R"===(

/*
 * Cross-thread data dependency pattern
 * Each of 16 threads reads from 4 specific threads (including itself)
 */
static const __constant uint SHUFFLE_PATTERN[16][4] = {
	{0, 1, 2, 3},
	{0, 2, 3, 1},
	{0, 3, 1, 2},
	{0, 3, 2, 1},

	{1, 0, 2, 3},
	{1, 2, 3, 0},
	{1, 3, 0, 2},
	{1, 3, 2, 0},

	{2, 1, 0, 3},
	{2, 0, 3, 1},
	{2, 3, 1, 0},
	{2, 3, 0, 1},

	{3, 1, 2, 0},
	{3, 2, 0, 1},
	{3, 0, 1, 2},
	{3, 0, 2, 1}
};

/*
 * Per-thread initial counter values (IEEE 754 float32)
 * All values in [1.25, 1.5] for numerical stability
 */
static const __constant float THREAD_CONSTANTS[16] = {
	1.34375f,
	1.28125f,
	1.359375f,
	1.3671875f,

	1.4296875f,
	1.3984375f,
	1.3828125f,
	1.3046875f,

	1.4140625f,
	1.2734375f,
	1.2578125f,
	1.2890625f,

	1.3203125f,
	1.3515625f,
	1.3359375f,
	1.4609375f
};

/* Per-hash shared memory: 16 output vectors + 16 float accumulators */
struct SharedMemory
{
	int4 computation_output[16];
	float4 fp_accumulators[16];
};

/*
 * Phase 3 Kernel: GPU Floating-Point Computation
 *
 * 16 threads per hash cooperate via local memory.
 * Each iteration:
 *   1. Load 64 bytes from scratchpad (4 groups × 16 bytes)
 *   2. Compute FP chain using cross-thread shuffled data
 *   3. XOR results back into scratchpad
 *   4. Reduce accumulators to compute next scratchpad address
 */
__attribute__((reqd_work_group_size(WORKSIZE * 16, 1, 1)))
__kernel void cn_gpu_phase3_compute(__global int *scratchpad_in, __global int *state_buffer, uint numThreads)
{
	const uint gIdx = getIdx();

#if(COMP_MODE==1)
	if(gIdx/16 >= numThreads)
		return;
#endif

	// Minimal printf to act as memory barrier / synchronization point
	if (gIdx == 0) {
		printf(".");
	}

	uint chunk = get_local_id(0) / 16;

	__global int* scratchpad = (__global int*)((__global char*)scratchpad_in + MEMORY * (gIdx/16));

	__local struct SharedMemory smem_in[WORKSIZE];
	__local struct SharedMemory* smem = smem_in + chunk;

	uint tid = get_local_id(0) % 16;

	uint idxHash = gIdx/16;
	uint s = ((__global uint*)state_buffer)[idxHash * 50] >> 8;
	float4 fp_accumulator = (float4)(0);

	/* group_index: which 4-thread group (0-3) within 16-thread hash team */
	const uint group_index = tid / 4;
	/* lane_index: position within the group (0-3) */
	const uint lane_index = tid % 4;
	const uint block = group_index * 16 + lane_index;

	#pragma unroll CN_UNROLL
	for(size_t i = 0; i < ITERATIONS; i++)
	{
		/* Step 1: Load 64 bytes from scratchpad into local memory */
		mem_fence(CLK_LOCAL_MEM_FENCE);
		int tmp = ((__global int*)scratchpad_ptr(s, group_index, scratchpad))[lane_index];
		((__local int*)(smem->computation_output))[tid] = tmp;
		mem_fence(CLK_LOCAL_MEM_FENCE);

		/* Step 2: Compute FP chain using cross-thread shuffled data */
		{
			compute_fp_chain_rotated(
				lane_index,
				*(smem->computation_output + SHUFFLE_PATTERN[tid][0]),
				*(smem->computation_output + SHUFFLE_PATTERN[tid][1]),
				*(smem->computation_output + SHUFFLE_PATTERN[tid][2]),
				*(smem->computation_output + SHUFFLE_PATTERN[tid][3]),
				THREAD_CONSTANTS[tid], fp_accumulator, smem->fp_accumulators + tid,
				smem->computation_output + tid
			);
		}
		mem_fence(CLK_LOCAL_MEM_FENCE);

		/* Step 3: XOR-reduce within group and write back to scratchpad */
		int outXor = ((__local int*)smem->computation_output)[block];
		for(uint dd = block + 4; dd < (group_index + 1) * 16; dd += 4)
			outXor ^= ((__local int*)smem->computation_output)[dd];

		((__global int*)scratchpad_ptr(s, group_index, scratchpad))[lane_index] = outXor ^ tmp;
		((__local int*)smem->computation_output)[tid] = outXor;

		/* Step 4: Reduce float accumulators within groups */
		float va_tmp1 = ((__local float*)smem->fp_accumulators)[block] + ((__local float*)smem->fp_accumulators)[block + 4];
		float va_tmp2 = ((__local float*)smem->fp_accumulators)[block+ 8] + ((__local float*)smem->fp_accumulators)[block + 12];
		((__local float*)smem->fp_accumulators)[tid] = va_tmp1 + va_tmp2;

		mem_fence(CLK_LOCAL_MEM_FENCE);

		/* Step 5: Cross-group XOR and final accumulator reduction */
		int out2 = ((__local int*)smem->computation_output)[tid] ^ ((__local int*)smem->computation_output)[tid + 4 ] ^ ((__local int*)smem->computation_output)[tid + 8] ^ ((__local int*)smem->computation_output)[tid + 12];
		va_tmp1 = ((__local float*)smem->fp_accumulators)[block] + ((__local float*)smem->fp_accumulators)[block + 4];
		va_tmp2 = ((__local float*)smem->fp_accumulators)[block + 8] + ((__local float*)smem->fp_accumulators)[block + 12];
		va_tmp1 = va_tmp1 + va_tmp2;
		va_tmp1 = fabs(va_tmp1);

		/* Step 6: Compute next scratchpad address */
		float xx = va_tmp1 * 16777216.0f;  /* FP_NORMALIZE_SCALE */
		int xx_int = (int)xx;
		((__local int*)smem->computation_output)[tid] = out2 ^ xx_int;
		((__local float*)smem->fp_accumulators)[tid] = va_tmp1 / 64.0f;  /* FP_RANGE_DIVISOR */

		mem_fence(CLK_LOCAL_MEM_FENCE);

		fp_accumulator = smem->fp_accumulators[0];
		s = smem->computation_output[0].x ^ smem->computation_output[0].y ^ smem->computation_output[0].z ^ smem->computation_output[0].w;
	}
}

)==="
	R"===(

/* Bytes per keccak output chunk: 160, 176, 176 = 512 total */
static const __constant uint skip[3] = {
	20,22,22
};

/* Generate one 512-byte scratchpad chunk from Keccak state */
inline void generate_512_bytes(uint idx, __local ulong* in, __global ulong* out)
{
	ulong hash[25];

	hash[0] = in[0] ^ idx;
	for(int i = 1; i < 25; ++i)
		hash[i] = in[i];

	for(int a = 0; a < 3;++a)
	{
		keccakf1600_1(hash);
		for(int i = 0; i < skip[a]; ++i)
			out[i] = hash[i];
		out+=skip[a];
	}
}

/*
 * Phase 1 Kernel (part 1): Keccak hash of input → 200-byte state
 */
__attribute__((reqd_work_group_size(8, 8, 1)))
__kernel void cn_gpu_phase1_keccak(__global ulong *input, __global int *Scratchpad, __global ulong *states, uint Threads)
{
    const uint gIdx = getIdx();
    __local ulong State_buf[8 * 25];
	__local ulong* State = State_buf + get_local_id(0) * 25;

#if(COMP_MODE==1)
    // do not use early return here
	if(gIdx < Threads)
#endif
    {
        states += 25 * gIdx;
        Scratchpad = (__global int*)((__global char*)Scratchpad + MEMORY * gIdx);

        if (get_local_id(1) == 0)
        {

// NVIDIA
#ifdef __NV_CL_C_VERSION
			for(uint i = 0; i < 8; ++i)
				State[i] = input[i];
#else
            ((__local ulong8 *)State)[0] = vload8(0, input);
#endif
            State[8]  = input[8];
            State[9]  = input[9];
            State[10] = input[10];

            /* Patch nonce into input at bytes 39-42 */
            ((__local uint *)State)[9]  &= 0x00FFFFFFU;
            ((__local uint *)State)[9]  |= (((uint)get_global_id(0)) & 0xFF) << 24;
            ((__local uint *)State)[10] &= 0xFF000000U;
            /* explicit cast to `uint` required for some OpenCL implementations */
            ((__local uint *)State)[10] |= (((uint)get_global_id(0) >> 8));

            for (int i = 11; i < 25; ++i) {
                State[i] = 0x00UL;
            }

            /* Keccak padding */
            State[16] = 0x8000000000000000UL;

            keccakf1600_2(State);

            #pragma unroll
            for (int i = 0; i < 25; ++i) {
                states[i] = State[i];
            }
        }
	}
}

/*
 * Phase 2 Kernel: Expand 200-byte state into 2MB scratchpad (Keccak-based)
 */
__attribute__((reqd_work_group_size(64, 1, 1)))
__kernel void cn_gpu_phase2_expand(__global int *Scratchpad, __global ulong *states)
{
    const uint gIdx = getIdx() / 64;
    __local ulong State[25];

	states += 25 * gIdx;
    Scratchpad = (__global int*)((__global char*)Scratchpad + MEMORY * gIdx);

	for(int i = get_local_id(0); i < 25; i+=get_local_size(0))
		State[i] = states[i];

	barrier(CLK_LOCAL_MEM_FENCE);

	for(uint i = get_local_id(0); i < MEMORY / 512; i += get_local_size(0))
	{
		generate_512_bytes(i, State, (__global ulong*)((__global uchar*)Scratchpad + i*512));
	}
}

)==="
