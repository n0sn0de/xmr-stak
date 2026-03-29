#pragma once

// CUDA version compatibility shims
// CUDA 12.x removed legacy shorthand intrinsics that were available in CUDA 7-11.
// This header provides version-gated compatibility macros.

#include <cuda_runtime.h>

#if CUDART_VERSION >= 12000
// int2float was a shorthand for __int2float_rn (round-to-nearest)
#ifndef int2float
#define int2float(x) __int2float_rn(x)
#endif

// float_as_int / int_as_float were shorthands for type-punning intrinsics
#ifndef float_as_int
#define float_as_int(x) __float_as_int(x)
#endif

#ifndef int_as_float
#define int_as_float(x) __int_as_float(x)
#endif
#endif // CUDART_VERSION >= 12000
