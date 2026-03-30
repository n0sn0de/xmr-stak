#pragma once

/// Keccak-1600 hash function (SHA-3 predecessor)
/// Used as the initial hash in the CryptoNight-GPU algorithm.
///
/// Original: Markku-Juhani O. Saarinen <mjos@iki.fi>, 19-Nov-2011

#include <cstdint>
#include <cstddef>

extern "C"
{
	/// Keccak-f[1600] permutation
	void keccakf(uint64_t st[25], int rounds);

	/// Compute Keccak hash of arbitrary length
	void keccak(const uint8_t* in, int inlen, uint8_t* md, int mdlen);

	/// Keccak-1600: full 200-byte state output
	void keccak1600(const uint8_t* in, int inlen, uint8_t* md);
}
