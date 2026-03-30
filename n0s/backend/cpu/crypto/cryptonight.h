#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "n0s/backend/cryptonight.hpp"

#if defined _MSC_VER
#define ABI_ATTRIBUTE
#else
#define ABI_ATTRIBUTE __attribute__((ms_abi))
#endif

struct cryptonight_ctx;

typedef void (*cn_mainloop_fun)(cryptonight_ctx* ctx);
typedef void (*cn_double_mainloop_fun)(cryptonight_ctx*, cryptonight_ctx*);
typedef void (*cn_hash_fun)(const void*, size_t, void*, cryptonight_ctx**, const n0s_algo&);

struct cryptonight_ctx
{
	uint8_t hash_state[224]; // Need only 200, explicit align
	uint8_t* long_state;
	uint8_t ctx_info[24]; //Use some of the extra memory for flags
	cn_mainloop_fun loop_fn = nullptr;
	cn_hash_fun hash_fn = nullptr;
	uint8_t* fun_data = nullptr;
};

struct alloc_msg
{
	const char* warning;
};

size_t cryptonight_init(size_t use_fast_mem, size_t use_mlock, alloc_msg* msg);
cryptonight_ctx* cryptonight_alloc_ctx(size_t use_fast_mem, size_t use_mlock, alloc_msg* msg);
void cryptonight_free_ctx(cryptonight_ctx* ctx);
