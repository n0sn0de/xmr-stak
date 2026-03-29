/**
 * cryptonight_common.cpp — Memory allocation for CryptoNight-GPU
 *
 * Handles scratchpad memory allocation with huge page (2MB) support.
 * cn_gpu uses a 2MB scratchpad per hash, ideally backed by MAP_HUGETLB.
 *
 * extra_hashes (Blake/Groestl/JH/Skein) removed — cn_gpu outputs first
 * 32 bytes of Keccak state directly without branch dispatch.
 *
 * Windows support removed — Linux only.
 */

#include "cryptonight.h"
#include "cryptonight_aesni.h"
#include "xmrstak/backend/cryptonight.hpp"
#include "xmrstak/jconf.hpp"
#include "xmrstak/misc/console.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __GNUC__
#include <mm_malloc.h>
#endif

#include <errno.h>
#include <sys/mman.h>

size_t cryptonight_init(size_t use_fast_mem, size_t use_mlock, alloc_msg* msg)
{
	// Linux: no special initialization needed
	return 1;
}

cryptonight_ctx* cryptonight_alloc_ctx(size_t use_fast_mem, size_t use_mlock, alloc_msg* msg)
{
	const size_t hashMemSize = ::jconf::inst()->GetMiningMemSize();

	cryptonight_ctx* ptr = (cryptonight_ctx*)_mm_malloc(sizeof(cryptonight_ctx), 4096);

	if(use_fast_mem == 0)
	{
		// Fallback: standard aligned allocation (no huge pages)
		ptr->long_state = (uint8_t*)_mm_malloc(hashMemSize, hashMemSize);
		ptr->ctx_info[0] = 0;
		ptr->ctx_info[1] = 0;
		if(ptr->long_state == NULL)
			printer::inst()->print_msg(L0, "MEMORY ALLOC FAILED: _mm_malloc was not able to allocate %s byte",
				std::to_string(hashMemSize).c_str());
		return ptr;
	}

	// Preferred: mmap with huge pages (MAP_HUGETLB)
	ptr->long_state = (uint8_t*)mmap(NULL, hashMemSize, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);

	if(ptr->long_state == MAP_FAILED)
	{
		// Fallback: mmap without huge pages
		msg->warning = "mmap with HUGETLB failed, attempting without it (you should fix your kernel)";
		ptr->long_state = (uint8_t*)mmap(NULL, hashMemSize, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	}

	if(ptr->long_state == MAP_FAILED)
	{
		_mm_free(ptr);
		msg->warning = "mmap failed, check attribute 'use_slow_memory' in 'config.txt'";
		return NULL;
	}

	ptr->ctx_info[0] = 1;

	if(madvise(ptr->long_state, hashMemSize, MADV_RANDOM | MADV_WILLNEED) != 0)
		msg->warning = "madvise failed";

	ptr->ctx_info[1] = 0;
	if(use_mlock != 0 && mlock(ptr->long_state, hashMemSize) != 0)
		msg->warning = "mlock failed";
	else
		ptr->ctx_info[1] = 1;

	return ptr;
}

void cryptonight_free_ctx(cryptonight_ctx* ctx)
{
	const size_t hashMemSize = ::jconf::inst()->GetMiningMemSize();

	if(ctx->ctx_info[0] != 0)
	{
		if(ctx->ctx_info[1] != 0)
			munlock(ctx->long_state, hashMemSize);
		munmap(ctx->long_state, hashMemSize);
	}
	else
	{
		_mm_free(ctx->long_state);
	}

	_mm_free(ctx);
}
