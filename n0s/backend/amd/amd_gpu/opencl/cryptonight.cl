R"===(
/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */

// Algorithm ID — passed via -DALGO=13 compiler option.
// Kernel names are now explicit (cn_gpu_phase*) and don't use ALGO.


static const __constant ulong keccakf_rndc[24] =
{
	0x0000000000000001, 0x0000000000008082, 0x800000000000808a,
	0x8000000080008000, 0x000000000000808b, 0x0000000080000001,
	0x8000000080008081, 0x8000000000008009, 0x000000000000008a,
	0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
	0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
	0x8000000000008003, 0x8000000000008002, 0x8000000000000080,
	0x000000000000800a, 0x800000008000000a, 0x8000000080008081,
	0x8000000000008080, 0x0000000080000001, 0x8000000080008008
};

static const __constant uchar sbox[256] =
{
	0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
	0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
	0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
	0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
	0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
	0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
	0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
	0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
	0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
	0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
	0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
	0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
	0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
	0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
	0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
	0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

//#include "opencl/wolf-aes.cl"
N0S_INCLUDE_WOLF_AES

// keccakf1600_1: private memory — used by cn_gpu Phase 1 (Keccak) and Phase 2 (expand)
// keccakf1600_2: local memory — used by cn_gpu Phase 4/5 (finalize)

static const __constant uint keccakf_rotc[24] =
{
	1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
	27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
};

static const __constant uint keccakf_piln[24] =
{
	10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
	15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
};

// keccakf1600_1: private memory version, used by cn0_cn_gpu kernel (Phase 1)
inline void keccakf1600_1(ulong st[25])
{
	int i, round;
	ulong t, bc[5];

	#pragma unroll 1
	for (round = 0; round < 24; ++round)
	{
		bc[0] = st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20] ^ rotate(st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22], 1UL);
		bc[1] = st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21] ^ rotate(st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23], 1UL);
		bc[2] = st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22] ^ rotate(st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24], 1UL);
		bc[3] = st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23] ^ rotate(st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20], 1UL);
		bc[4] = st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24] ^ rotate(st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21], 1UL);

		st[0] ^= bc[4];
		st[5] ^= bc[4];
		st[10] ^= bc[4];
		st[15] ^= bc[4];
		st[20] ^= bc[4];

		st[1] ^= bc[0];
		st[6] ^= bc[0];
		st[11] ^= bc[0];
		st[16] ^= bc[0];
		st[21] ^= bc[0];

		st[2] ^= bc[1];
		st[7] ^= bc[1];
		st[12] ^= bc[1];
		st[17] ^= bc[1];
		st[22] ^= bc[1];

		st[3] ^= bc[2];
		st[8] ^= bc[2];
		st[13] ^= bc[2];
		st[18] ^= bc[2];
		st[23] ^= bc[2];

		st[4] ^= bc[3];
		st[9] ^= bc[3];
		st[14] ^= bc[3];
		st[19] ^= bc[3];
		st[24] ^= bc[3];

		// Rho Pi
		t = st[1];
		#pragma unroll
		for (i = 0; i < 24; ++i) {
			bc[0] = st[keccakf_piln[i]];
			st[keccakf_piln[i]] = rotate(t, (ulong)keccakf_rotc[i]);
			t = bc[0];
		}

		#pragma unroll
		for(int i = 0; i < 25; i += 5)
		{
			ulong tmp1 = st[i], tmp2 = st[i + 1];

			st[i] = bitselect(st[i] ^ st[i + 2], st[i], st[i + 1]);
			st[i + 1] = bitselect(st[i + 1] ^ st[i + 3], st[i + 1], st[i + 2]);
			st[i + 2] = bitselect(st[i + 2] ^ st[i + 4], st[i + 2], st[i + 3]);
			st[i + 3] = bitselect(st[i + 3] ^ tmp1, st[i + 3], st[i + 4]);
			st[i + 4] = bitselect(st[i + 4] ^ tmp2, st[i + 4], tmp1);
		}

		//  Iota
		st[0] ^= keccakf_rndc[round];
	}
}

// keccakf1600_2: local memory version, used by cn2 kernel (Phase 5: Finalize)
void keccakf1600_2(__local ulong *st)
{
	int i, round;
	ulong t, bc[5];

	#pragma unroll 1
	for (round = 0; round < 24; ++round)
	{
		bc[0] = st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20] ^ rotate(st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22], 1UL);
		bc[1] = st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21] ^ rotate(st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23], 1UL);
		bc[2] = st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22] ^ rotate(st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24], 1UL);
		bc[3] = st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23] ^ rotate(st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20], 1UL);
		bc[4] = st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24] ^ rotate(st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21], 1UL);

		st[0] ^= bc[4];
		st[5] ^= bc[4];
		st[10] ^= bc[4];
		st[15] ^= bc[4];
		st[20] ^= bc[4];

		st[1] ^= bc[0];
		st[6] ^= bc[0];
		st[11] ^= bc[0];
		st[16] ^= bc[0];
		st[21] ^= bc[0];

		st[2] ^= bc[1];
		st[7] ^= bc[1];
		st[12] ^= bc[1];
		st[17] ^= bc[1];
		st[22] ^= bc[1];

		st[3] ^= bc[2];
		st[8] ^= bc[2];
		st[13] ^= bc[2];
		st[18] ^= bc[2];
		st[23] ^= bc[2];

		st[4] ^= bc[3];
		st[9] ^= bc[3];
		st[14] ^= bc[3];
		st[19] ^= bc[3];
		st[24] ^= bc[3];

		// Rho Pi
		t = st[1];
		#pragma unroll
		for (i = 0; i < 24; ++i) {
			bc[0] = st[keccakf_piln[i]];
			st[keccakf_piln[i]] = rotate(t, (ulong)keccakf_rotc[i]);
			t = bc[0];
		}

		#pragma unroll
		for(int i = 0; i < 25; i += 5)
		{
			ulong tmp1 = st[i], tmp2 = st[i + 1];

			st[i] = bitselect(st[i] ^ st[i + 2], st[i], st[i + 1]);
			st[i + 1] = bitselect(st[i + 1] ^ st[i + 3], st[i + 1], st[i + 2]);
			st[i + 2] = bitselect(st[i + 2] ^ st[i + 4], st[i + 2], st[i + 3]);
			st[i + 3] = bitselect(st[i + 3] ^ tmp1, st[i + 3], st[i + 4]);
			st[i + 4] = bitselect(st[i + 4] ^ tmp2, st[i + 4], tmp1);
		}

		//  Iota
		st[0] ^= keccakf_rndc[round];
	}
}




inline uint getIdx()
{
	return get_global_id(0) - get_global_offset(0);
}

// CryptoNight-GPU always uses direct (non-strided) scratchpad indexing
#define IDX(x)	(x)
static const __constant uchar rcon[8] = { 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 };

#define SubWord(inw)		((sbox[BYTE(inw, 3)] << 24) | (sbox[BYTE(inw, 2)] << 16) | (sbox[BYTE(inw, 1)] << 8) | sbox[BYTE(inw, 0)])

void AESExpandKey256(uint *keybuf)
{
	//#pragma unroll 4
	for(uint c = 8, i = 1; c < 40; ++c)
	{
		// For 256-bit keys, an sbox permutation is done every other 4th uint generated, AND every 8th
		uint t = ((!(c & 7)) || ((c & 7) == 4)) ? SubWord(keybuf[c - 1]) : keybuf[c - 1];

		// If the uint we're generating has an index that is a multiple of 8, rotate and XOR with the round constant,
		// then XOR this with previously generated uint. If it's 4 after a multiple of 8, only the sbox permutation
		// is done, followed by the XOR. If neither are true, only the XOR with the previously generated uint is done.
		keybuf[c] = keybuf[c - 8] ^ ((!(c & 7)) ? rotate(t, 24U) ^ as_uint((uchar4)(rcon[i++], 0U, 0U, 0U)) : t);
	}
}


// Float4 helper functions for cn_gpu FP computation kernel
inline float4 _mm_add_ps(float4 a, float4 b) { return a + b; }
inline float4 _mm_sub_ps(float4 a, float4 b) { return a - b; }
inline float4 _mm_mul_ps(float4 a, float4 b) { return a * b; }
inline float4 _mm_div_ps(float4 a, float4 b) { return a / b; }
inline float4 _mm_and_ps(float4 a, int b) { return as_float4(as_int4(a) & (int4)(b)); }
inline float4 _mm_or_ps(float4 a, int b) { return as_float4(as_int4(a) | (int4)(b)); }

inline float4 _mm_fmod_ps(float4 v, float dc)
{
	float4 d = (float4)(dc);
	float4 c = _mm_div_ps(v, d);
	c = trunc(c);
	c = _mm_mul_ps(c, d);
	return _mm_sub_ps(v, c);
}

inline int4 _mm_xor_si128(int4 a, int4 b) { return a ^ b; }
inline float4 _mm_xor_ps(float4 a, int b) { return as_float4(as_int4(a) ^ (int4)(b)); }

inline int4 _mm_alignr_epi8(int4 a, const uint rot)
{
	const uint right = 8 * rot;
	const uint left = (32 - 8 * rot);
	return (int4)(
		((uint)a.x >> right) | ( a.y << left ),
		((uint)a.y >> right) | ( a.z << left ),
		((uint)a.z >> right) | ( a.w << left ),
		((uint)a.w >> right) | ( a.x << left )
	);
}

// cn_gpu-specific kernels (Phase 1: Keccak, Phase 2: Expand, Phase 3: FP compute)
N0S_INCLUDE_CN_GPU



)==="
R"===(
#if defined(__clang__)
#	if __has_builtin(__builtin_amdgcn_ds_bpermute)
#		define HAS_AMD_BPERMUTE  1
#	endif
#endif

__attribute__((reqd_work_group_size(8, WORKSIZE, 1)))
__kernel void cn_gpu_phase4_finalize (__global uint4 *Scratchpad, __global ulong *states,
	__global uint *output, ulong Target, uint Threads)
{
    __local uint AES0[256], AES1[256], AES2[256], AES3[256];
    uint ExpandedKey2[40];
    uint4 text;

    uint gIdx = get_global_id(1) - get_global_offset(1);
    uint groupIdx = get_local_id(1);
    uint lIdx = get_local_id(0);

    for (int i = groupIdx * 8 + lIdx; i < 256; i += get_local_size(0) * get_local_size(1)) {
        const uint tmp = AES0_C[i];
        AES0[i] = tmp;
        AES1[i] = rotate(tmp, 8U);
        AES2[i] = rotate(tmp, 16U);
        AES3[i] = rotate(tmp, 24U);
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    __local uint4 xin1[WORKSIZE][8];
    __local uint4 xin2[WORKSIZE][8];

#if(COMP_MODE==1)
    // do not use early return here
    if(gIdx < Threads)
#endif
    {
        states += 25 * gIdx;
        Scratchpad += gIdx * (MEMORY >> 4);

        #if defined(__Tahiti__) || defined(__Pitcairn__)

        for(int i = 0; i < 4; ++i) ((ulong *)ExpandedKey2)[i] = states[i + 4];
        text = vload4(lIdx + 4, (__global uint *)states);

        #else
        text = vload4(lIdx + 4, (__global uint *)states);
        ((uint8 *)ExpandedKey2)[0] = vload8(1, (__global uint *)states);

        #endif

        AESExpandKey256(ExpandedKey2);
    }

    barrier(CLK_LOCAL_MEM_FENCE);

#if (HAS_AMD_BPERMUTE == 1)
	int lane = (groupIdx * 8 + ((lIdx + 1) % 8)) << 2;
	uint4 tmp = (uint4)(0, 0, 0, 0);
#else
    __local uint4* xin1_store = &xin1[groupIdx][lIdx];
    __local uint4* xin1_load = &xin1[groupIdx][(lIdx + 1) % 8];
    __local uint4* xin2_store = &xin2[groupIdx][lIdx];
    __local uint4* xin2_load = &xin2[groupIdx][(lIdx + 1) % 8];
    *xin2_store = (uint4)(0, 0, 0, 0);
#endif

#if(COMP_MODE == 1)
    // do not use early return here
    if (gIdx < Threads)
#endif
    {

#if (HAS_AMD_BPERMUTE == 1)
        #pragma unroll 2
        for(int i = 0, i1 = lIdx; i < (MEMORY >> 7); ++i, i1 = (i1 + 16) % (MEMORY >> 4))
        {
            text ^= Scratchpad[IDX((uint)i1)];
			text ^= tmp;

            #pragma unroll 10
            for(int j = 0; j < 10; ++j)
                text = AES_Round(AES0, AES1, AES2, AES3, text, ((uint4 *)ExpandedKey2)[j]);

            text.s0 ^= __builtin_amdgcn_ds_bpermute(lane, text.s0);
            text.s1 ^= __builtin_amdgcn_ds_bpermute(lane, text.s1);
            text.s2 ^= __builtin_amdgcn_ds_bpermute(lane, text.s2);
            text.s3 ^= __builtin_amdgcn_ds_bpermute(lane, text.s3);
            text ^= Scratchpad[IDX((uint)i1 + 8u)];

            #pragma unroll 10
            for(int j = 0; j < 10; ++j)
                text = AES_Round(AES0, AES1, AES2, AES3, text, ((uint4 *)ExpandedKey2)[j]);
            tmp.s0 = __builtin_amdgcn_ds_bpermute(lane, text.s0);
            tmp.s1 = __builtin_amdgcn_ds_bpermute(lane, text.s1);
            tmp.s2 = __builtin_amdgcn_ds_bpermute(lane, text.s2);
            tmp.s3 = __builtin_amdgcn_ds_bpermute(lane, text.s3);
        }

        text ^= tmp;
#else

		#pragma unroll 2
		for(int i = 0, i1 = lIdx; i < (MEMORY >> 7); ++i, i1 = (i1 + 16) % (MEMORY >> 4))
		{
			text ^= Scratchpad[IDX((uint)i1)];
			barrier(CLK_LOCAL_MEM_FENCE);
			text ^= *xin2_load;
			#pragma unroll 10
			for(int j = 0; j < 10; ++j)
			    text = AES_Round(AES0, AES1, AES2, AES3, text, ((uint4 *)ExpandedKey2)[j]);
			*xin1_store = text;
			text ^= Scratchpad[IDX((uint)i1 + 8u)];
			barrier(CLK_LOCAL_MEM_FENCE);
			text ^= *xin1_load;

			#pragma unroll 10
			for(int j = 0; j < 10; ++j)
			    text = AES_Round(AES0, AES1, AES2, AES3, text, ((uint4 *)ExpandedKey2)[j]);

			*xin2_store = text;
		}

        barrier(CLK_LOCAL_MEM_FENCE);
        text ^= *xin2_load;
#endif
    }

    /* Also left over threads performe this loop.
     * The left over thread results will be ignored
     */
    #pragma unroll 16
    for(size_t i = 0; i < 16; i++)
    {
        #pragma unroll 10
        for (int j = 0; j < 10; ++j) {
            text = AES_Round(AES0, AES1, AES2, AES3, text, ((uint4 *)ExpandedKey2)[j]);
        }
#if (HAS_AMD_BPERMUTE == 1)
	    text.s0 ^= __builtin_amdgcn_ds_bpermute(lane, text.s0);
        text.s1 ^= __builtin_amdgcn_ds_bpermute(lane, text.s1);
        text.s2 ^= __builtin_amdgcn_ds_bpermute(lane, text.s2);
        text.s3 ^= __builtin_amdgcn_ds_bpermute(lane, text.s3);
#else
        barrier(CLK_LOCAL_MEM_FENCE);
        *xin1_store = text;
        barrier(CLK_LOCAL_MEM_FENCE);
        text ^= *xin1_load;
#endif
    }

    __local ulong State_buf[8 * 25];
#if(COMP_MODE==1)
    // do not use early return here
    if(gIdx < Threads)
#endif
    {
        vstore2(as_ulong2(text), lIdx + 4, states);
    }

    barrier(CLK_GLOBAL_MEM_FENCE);

#if(COMP_MODE==1)
    // do not use early return here
    if(gIdx < Threads)
#endif
    {
        if(!lIdx)
        {
            __local ulong* State = State_buf + groupIdx * 25;

            for(int i = 0; i < 25; ++i) State[i] = states[i];

            keccakf1600_2(State);

			if(State[3] <= Target)
			{
				ulong outIdx = atomic_inc(output + 0xFF);
				if(outIdx < 0xFF)
					output[outIdx] = get_global_id(1);
			}
        }
    }
    mem_fence(CLK_GLOBAL_MEM_FENCE);
}

)==="
