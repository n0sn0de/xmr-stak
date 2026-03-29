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
			// use a waitable timer for larger intervals > 0.1ms

			HANDLE timer;
			LARGE_INTEGER ft;

			ft.QuadPart = -10ll * int64_t(waitTime); // Convert to 100 nanosecond interval, negative value indicates relative time

			timer = CreateWaitableTimer(NULL, TRUE, NULL);
			SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
			WaitForSingleObject(timer, INFINITE);
			CloseHandle(timer);
		}
		else
		{
			// use a polling loop for short intervals <= 100ms

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

#include "cryptonight.hpp"
#include "cuda_aes.hpp"
#include "cuda_device.hpp"
#include "cuda_extra.hpp"

/* sm_2X is limited to 2GB due to the small TLB
 * therefore we never use 64bit indices
 */
#if defined(XMR_STAK_LARGEGRID) && (__CUDA_ARCH__ >= 300)
typedef uint64_t IndexType;
#else
typedef int IndexType;
#endif

/** avoid warning `unused parameter` */
template <typename T>
__forceinline__ __device__ void unusedVar(const T&)
{
}

/** shuffle data for
 *
 * - this method can be used with all compute architectures
 * - for <sm_30 shared memory is needed
 *
 * group_n - must be a power of 2!
 *
 * @param ptr pointer to shared memory, size must be `threadIdx.x * sizeof(uint32_t)`
 *            value can be NULL for compute architecture >=sm_30
 * @param sub thread number within the group, range [0:group_n]
 * @param value value to share with other threads within the group
 * @param src thread number within the group from where the data is read, range [0:group_n]
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

template <xmrstak_algo_id ALGO>
__global__ void cryptonight_core_gpu_phase3(
	const uint32_t ITERATIONS, const size_t MEMORY,
	int threads, int bfactor, int partidx, uint32_t* long_stateIn, const uint32_t* const __restrict__ d_ctx_stateIn, uint32_t* __restrict__ d_ctx_key2)
{
	__shared__ uint32_t sharedMemoryX[256 * 32];

	/* avoid that the compiler is later in the aes round optimizing `sharedMemory[ x * 32 ]` to `sharedMemoryX + x * 32 + twidx`*/
	const int twidx = (threadIdx.x * 4) % 128;
	// this is equivalent to `(uint32_t*)sharedMemoryX + twidx;` where `twidx` is [0;32)
	char* sharedMemory = (char*)sharedMemoryX + twidx;

	cn_aes_gpu_init32(sharedMemoryX);
	__syncthreads();

	int thread = (blockDim.x * blockIdx.x + threadIdx.x) >> 3;
	int subv = (threadIdx.x & 7);
	int sub = subv << 2;

	const int batchsize = MEMORY >> bfactor;
	const int start = (partidx % (1 << bfactor)) * batchsize;
	const int end = start + batchsize;

	if(thread >= threads)
		return;

	const uint32_t* const long_state = long_stateIn + ((IndexType)thread * MEMORY) + sub;

	uint32_t key[40], text[4];
	#pragma unroll 10
	for(int j = 0; j < 10; ++j)
		((ulonglong4*)key)[j] = ((ulonglong4*)(d_ctx_key2 + thread * 40))[j];

	uint64_t* d_ctx_state = (uint64_t*)(d_ctx_stateIn + thread * 50 + sub + 16);
	#pragma unroll 2
	for(int j = 0; j < 2; ++j)
		((uint64_t*)text)[j] = loadGlobal64<uint64_t>(d_ctx_state + j);

	__syncthreads();

#if(__CUDA_ARCH__ < 300)
	extern __shared__ uint32_t shuffleMem[];
	volatile uint32_t* sPtr = (volatile uint32_t*)(shuffleMem + (threadIdx.x & 0xFFFFFFF8));
#else
	volatile uint32_t* sPtr = NULL;
#endif

	for(int i = start; i < end; i += 32)
	{
		uint32_t tmp[4];
		((ulonglong2*)(tmp))[0] =  ((ulonglong2*)(long_state + i))[0];
		#pragma unroll 4
		for(int j = 0; j < 4; ++j)
			text[j] ^= tmp[j];

		((uint4*)text)[0] = cn_aes_pseudo_round_mut32((uint32_t*)sharedMemory, ((uint4*)text)[0], (uint4*)key);

		// cn_gpu uses 8-thread shuffle for mix_and_propagate equivalent
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

	#pragma unroll 2
	for(int j = 0; j < 2; ++j)
		storeGlobal64<uint64_t>(d_ctx_state + j, ((uint64_t*)text)[j]);
}

template <xmrstak_algo_id ALGO, uint32_t MEM_MODE>
void cryptonight_core_gpu_hash_gpu(nvid_ctx* ctx, uint32_t nonce, const xmrstak_algo& algo)
{
	const uint32_t MASK = algo.Mask();
	const uint32_t ITERATIONS = algo.Iter();
	const size_t MEM = algo.Mem();

	dim3 grid(ctx->device_blocks);
	dim3 block(ctx->device_threads);
	dim3 block8(ctx->device_threads << 3);

	size_t intensity = ctx->device_blocks * ctx->device_threads;

	CUDA_CHECK_KERNEL(
		ctx->device_id,
		xmrstak::nvidia::cn_explode_gpu<<<intensity, 128>>>(MEM, (int*)ctx->d_ctx_state, (int*)ctx->d_long_state));

	int partcount = 1 << ctx->device_bfactor;
	for(int i = 0; i < partcount; i++)
	{
		CUDA_CHECK_KERNEL(
			ctx->device_id,
			// 36 x 16byte x numThreads
			xmrstak::nvidia::cryptonight_core_gpu_phase2_gpu<<<ctx->device_blocks, ctx->device_threads * 16, 33 * 16 * ctx->device_threads>>>(
				ITERATIONS,
				MEM,
				MASK,
				(int*)ctx->d_ctx_state,
				(int*)ctx->d_long_state,
				ctx->device_bfactor,
				i,
				ctx->d_ctx_a,
				ctx->d_ctx_b));
	}

	/* bfactor for phase 3
	 *
	 * 3 consume less time than phase 2, therefore we begin with the
	 * kernel splitting if the user defined a `bfactor >= 8`
	 */
	int bfactorOneThree = ctx->device_bfactor - 8;
	if(bfactorOneThree < 0)
		bfactorOneThree = 0;

	int partcountOneThree = 1 << bfactorOneThree;
	// cn_gpu uses two full rounds over the scratchpad memory
	int roundsPhase3 = partcountOneThree * 2;

	int blockSizePhase3 = block8.x;
	int gridSizePhase3 = grid.x;
	if(blockSizePhase3 * 2 <= ctx->device_maxThreadsPerBlock)
	{
		blockSizePhase3 *= 2;
		gridSizePhase3 = (gridSizePhase3 + 1) / 2;
	}

	for(int i = 0; i < roundsPhase3; i++)
	{
		CUDA_CHECK_KERNEL(ctx->device_id, cryptonight_core_gpu_phase3<ALGO><<<
											  gridSizePhase3,
											  blockSizePhase3,
											  blockSizePhase3 * sizeof(uint32_t) * static_cast<int>(ctx->device_arch[0] < 3)>>>(
											  ITERATIONS,
											  MEM / 4,
											  ctx->device_blocks * ctx->device_threads,
											  bfactorOneThree, i,
											  ctx->d_long_state,
											  ctx->d_ctx_state, ctx->d_ctx_key2));
	}
}

void cryptonight_core_cpu_hash(nvid_ctx* ctx, const xmrstak_algo& miner_algo, uint32_t startNonce, uint64_t chain_height)
{
	if(miner_algo == invalid_algo)
		return;

	// Only cryptonight_gpu is supported
	if(ctx->memMode == 1)
		cryptonight_core_gpu_hash_gpu<cryptonight_gpu, 1>(ctx, startNonce, miner_algo);
	else
		cryptonight_core_gpu_hash_gpu<cryptonight_gpu, 0>(ctx, startNonce, miner_algo);
}
