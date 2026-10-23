/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef __LZ4DEFS_H__
#define __LZ4DEFS_H__

/*
 * lz4defs.h -- common and architecture specific defines for the kernel usage
 *
 * LZ4 - Fast LZ compression algorithm
 * Copyright (c) Yann Collet. All rights reserved.
 *
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 *	Ported to upstream LZ4 1.11 by:
 *	BanYeHanFeng
 */

#include <linux/build_bug.h>
#include <linux/bitops.h>
#include <linux/lz4.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/unaligned.h>

#define FORCE_INLINE		__always_inline
#define LZ4_FORCE_INLINE	static __always_inline
#define FORCE_NOINLINE		static noinline
#define LZ4_FORCE_NOINLINE	static noinline
#define LZ4_FORCE_O2

#define KB (1 << 10)
#define MB (1 << 20)
#define GB (1U << 30)

#define MINMATCH 4
#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT 12
#define MATCH_SAFEGUARD_DISTANCE ((2 * WILDCOPYLENGTH) - MINMATCH)
#define FASTLOOP_SAFE_DISTANCE 64
#define LZ4_DISTANCE_ABSOLUTE_MAX 65535
#define LZ4_DISTANCE_MAX LZ4_DISTANCE_ABSOLUTE_MAX

#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

#define LZ4_STATIC_ASSERT(c)	static_assert(c, #c)
#define assert(condition)	((void)0)
#define DEBUGLOG(l, ...)	{}
#define MEM_INIT(p, v, s)	memset((p), (v), (s))

typedef u8 BYTE;
typedef u16 U16;
typedef u32 U32;
typedef s32 S32;
typedef u64 U64;
typedef uintptr_t uptrval;
typedef unsigned long reg_t;

typedef enum {
	notLimited = 0,
	limitedOutput = 1,
	fillOutput = 2
} limitedOutput_directive;

typedef enum { clearedTable = 0, byPtr, byU32, byU16 } tableType_t;
typedef enum { noDict = 0, withPrefix64k, usingExtDict, usingDictCtx } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;
typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { decode_full_block = 0, partial_decode = 1 } earlyEnd_directive;

static FORCE_INLINE unsigned int LZ4_isLittleEndian(void)
{
	const union { U32 u; BYTE c[4]; } one = { 1 };

	return one.c[0];
}

static FORCE_INLINE int LZ4_isAligned(const void *ptr, size_t alignment)
{
	return ((size_t)ptr & (alignment - 1)) == 0;
}

static FORCE_INLINE U16 LZ4_read16(const void *ptr)
{
	return get_unaligned((const U16 *)ptr);
}

static FORCE_INLINE U32 LZ4_read32(const void *ptr)
{
	return get_unaligned((const U32 *)ptr);
}

static FORCE_INLINE U64 LZ4_read64(const void *ptr)
{
	return get_unaligned((const U64 *)ptr);
}

static FORCE_INLINE reg_t LZ4_read_ARCH(const void *ptr)
{
	return get_unaligned((const reg_t *)ptr);
}

static FORCE_INLINE void LZ4_write16(void *memPtr, U16 value)
{
	put_unaligned(value, (U16 *)memPtr);
}

static FORCE_INLINE void LZ4_write32(void *memPtr, U32 value)
{
	put_unaligned(value, (U32 *)memPtr);
}

static FORCE_INLINE U16 LZ4_readLE16(const void *memPtr)
{
	return get_unaligned_le16(memPtr);
}

static FORCE_INLINE U32 LZ4_readLE32(const void *memPtr)
{
	return get_unaligned_le32(memPtr);
}

static FORCE_INLINE U64 LZ4_readLE64(const void *memPtr)
{
	return get_unaligned_le64(memPtr);
}

static FORCE_INLINE void LZ4_writeLE16(void *memPtr, U16 value)
{
	put_unaligned_le16(value, memPtr);
}

#define LZ4_memcpy(dst, src, size)	__builtin_memcpy(dst, src, size)
#define LZ4_memmove(dst, src, size)	__builtin_memmove(dst, src, size)
#define LZ4_memset(p, v, s)		memset((p), (v), (s))

/*
 * customized variant of memcpy, which can overwrite up to 7 bytes beyond dstEnd
 */
static FORCE_INLINE void LZ4_wildCopy8(void *dstPtr, const void *srcPtr,
				       void *dstEnd)
{
	BYTE *d = (BYTE *)dstPtr;
	const BYTE *s = (const BYTE *)srcPtr;
	BYTE *const e = (BYTE *)dstEnd;

	do {
		LZ4_memcpy(d, s, 8);
		d += 8;
		s += 8;
	} while (d < e);
}


static FORCE_INLINE unsigned int LZ4_NbCommonBytes(reg_t val)
{
	assert(val != 0);
	if (LZ4_isLittleEndian())
		return __ffs(val) >> 3;
	return (BITS_PER_LONG - 1 - __fls(val)) >> 3;
}

static FORCE_INLINE unsigned int LZ4_count(const BYTE *pIn,
					   const BYTE *pMatch,
					   const BYTE *pInLimit)
{
	const BYTE *const pStart = pIn;
	const size_t stepSize = sizeof(reg_t);

	if (likely(pIn < pInLimit - (stepSize - 1))) {
		reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);

		if (!diff) {
			pIn += stepSize;
			pMatch += stepSize;
		} else {
			return LZ4_NbCommonBytes(diff);
		}
	}

	while (likely(pIn < pInLimit - (stepSize - 1))) {
		reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);

		if (!diff) {
			pIn += stepSize;
			pMatch += stepSize;
			continue;
		}
		pIn += LZ4_NbCommonBytes(diff);
		return (unsigned int)(pIn - pStart);
	}

	if ((stepSize == 8) && (pIn < (pInLimit - 3)) &&
	    (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
		pIn += 4;
		pMatch += 4;
	}
	if ((pIn < (pInLimit - 1)) &&
	    (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
		pIn += 2;
		pMatch += 2;
	}
	if ((pIn < pInLimit) && (*pMatch == *pIn))
		pIn++;

	return (unsigned int)(pIn - pStart);
}

/*
 * Streaming state seen by the implementation.
 *
 * include/linux/lz4.h keeps exposing the historical state layout, whose size
 * is part of the kernel module ABI (LZ4_MEM_COMPRESS / LZ4HC_MEM_COMPRESS).
 * Upstream layouts below fit in the very same amount of memory, so the
 * exported objects keep their size while the algorithm runs on its own view.
 */
typedef struct LZ4_kstream {
	U32 hashTable[LZ4_HASH_SIZE_U32];
	const BYTE *dictionary;
	const struct LZ4_kstream *dictCtx;
	U32 currentOffset;
	U32 tableType;
	U32 dictSize;
} LZ4_kstream_t;

typedef struct LZ4_khctx {
	U32 hashTable[LZ4HC_HASHTABLESIZE];
	U16 chainTable[LZ4HC_MAXD];
	const struct LZ4_khctx *dictCtx;
	const BYTE *end;
	const BYTE *prefixStart;
	const BYTE *dictStart;
	U32 dictLimit;
	U32 lowLimit;
	U32 nextToUpdate;
	short compressionLevel;
	s8 favorDecSpeed;
} LZ4_khctx_t;

#define LZ4_STREAM_STATE(s)	((LZ4_kstream_t *)(void *)(s))
#define LZ4_STREAMHC_STATE(s)	((LZ4_khctx_t *)(void *)(s))

LZ4_STATIC_ASSERT(sizeof(LZ4_kstream_t) == sizeof(LZ4_stream_t));
LZ4_STATIC_ASSERT(sizeof(LZ4_khctx_t) <= sizeof(LZ4_streamHC_t));

int LZ4_decompress_safe_withPrefix64k(const char *source, char *dest,
				      int compressedSize, int maxOutputSize);
int LZ4_decompress_fast_withPrefix64k(const char *source, char *dest,
				      int originalSize);
int LZ4_decompress_safe_forceExtDict(const char *source, char *dest,
				     int compressedSize, int maxOutputSize,
				     const void *dictStart, size_t dictSize);
int LZ4_decompress_safe_partial_forceExtDict(const char *source, char *dest,
					     int compressedSize, int targetOutputSize,
					     int dstCapacity, const void *dictStart,
					     size_t dictSize);
LZ4_streamDecode_t *LZ4_createStreamDecode(void);
int LZ4_freeStreamDecode(LZ4_streamDecode_t *LZ4_stream);
int LZ4_decoderRingBufferSize(int maxBlockSize);
int LZ4_decompress_safe_partial_usingDict(const char *source, char *dest,
					  int compressedSize, int targetOutputSize,
					  int dstCapacity, const char *dictStart,
					  int dictSize);

#endif
