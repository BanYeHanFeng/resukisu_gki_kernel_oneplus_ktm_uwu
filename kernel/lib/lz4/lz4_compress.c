// SPDX-License-Identifier: BSD-2-Clause
/*
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
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Ported to upstream LZ4 1.11.1 for kernel usage by:
 *	BanYeHanFeng
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/lz4.h>

#include "lz4defs.h"

#define LZ4_ACCELERATION_MAX 65537
#define LZ4_SKIPTRIGGER 6
#define HASH_UNIT sizeof(reg_t)

static const int LZ4_minLength = (MFLIMIT + 1);
static const int LZ4_64Klimit = ((64 * KB) + (MFLIMIT - 1));
static const int LZ4_skipTrigger = LZ4_SKIPTRIGGER;

LZ4_FORCE_INLINE U32 LZ4_hash4(U32 sequence, tableType_t const tableType)
{
	const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG + 1 : LZ4_HASHLOG;

	return (sequence * 2654435761U) >> ((MINMATCH * 8) - hashLog);
}

LZ4_FORCE_INLINE U32 LZ4_hash5(U64 sequence, tableType_t const tableType)
{
	const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG + 1 : LZ4_HASHLOG;

	if (LZ4_isLittleEndian()) {
		const U64 prime5bytes = 889523592379ULL;

		return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
	} else {
		const U64 prime8bytes = 11400714785074694791ULL;

		return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
	}
}

LZ4_FORCE_INLINE U32 LZ4_hashPosition(const void *const p,
				      tableType_t const tableType)
{
	if ((sizeof(reg_t) == 8) && (tableType != byU16))
		return LZ4_hash5(LZ4_read_ARCH(p), tableType);

	return LZ4_hash4(LZ4_read32(p), tableType);
}

LZ4_FORCE_INLINE void LZ4_clearHash(U32 h, void *tableBase,
				    tableType_t const tableType)
{
	switch (tableType) {
	default:
	case clearedTable:
		assert(0);
		return;
	case byPtr: {
		const BYTE **hashTable = (const BYTE **)tableBase;

		hashTable[h] = NULL;
		return;
	}
	case byU32: {
		U32 *hashTable = (U32 *)tableBase;

		hashTable[h] = 0;
		return;
	}
	case byU16: {
		U16 *hashTable = (U16 *)tableBase;

		hashTable[h] = 0;
		return;
	}
	}
}

LZ4_FORCE_INLINE void LZ4_putIndexOnHash(U32 idx, U32 h, void *tableBase,
					 tableType_t const tableType)
{
	switch (tableType) {
	default:
	case clearedTable:
	case byPtr:
		assert(0);
		return;
	case byU32: {
		U32 *hashTable = (U32 *)tableBase;

		hashTable[h] = idx;
		return;
	}
	case byU16: {
		U16 *hashTable = (U16 *)tableBase;

		assert(idx < 65536);
		hashTable[h] = (U16)idx;
		return;
	}
	}
}

LZ4_FORCE_INLINE void LZ4_putPositionOnHash(const BYTE *p, U32 h,
					    void *tableBase,
					    tableType_t const tableType)
{
	const BYTE **const hashTable = (const BYTE **)tableBase;

	assert(tableType == byPtr);
	(void)tableType;
	hashTable[h] = p;
}

LZ4_FORCE_INLINE void LZ4_putPosition(const BYTE *p, void *tableBase,
				      tableType_t tableType, const BYTE *base)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	if (tableType == byPtr)
		LZ4_putPositionOnHash(p, h, tableBase, byPtr);
	else
		LZ4_putIndexOnHash((U32)(p - base), h, tableBase, tableType);
}

LZ4_FORCE_INLINE U32 LZ4_getIndexOnHash(U32 h, const void *tableBase,
					tableType_t tableType)
{
	LZ4_STATIC_ASSERT(LZ4_MEMORY_USAGE > 2);
	if (tableType == byU32) {
		const U32 *const hashTable = (const U32 *)tableBase;

		assert(h < (1U << (LZ4_MEMORY_USAGE - 2)));
		return hashTable[h];
	}
	if (tableType == byU16) {
		const U16 *const hashTable = (const U16 *)tableBase;

		assert(h < (1U << (LZ4_MEMORY_USAGE - 1)));
		return hashTable[h];
	}
	assert(0);
	return 0;
}

static FORCE_INLINE const BYTE *LZ4_getPositionOnHash(U32 h,
						      const void *tableBase,
						      tableType_t tableType)
{
	const BYTE *const *hashTable = (const BYTE *const *)tableBase;

	assert(tableType == byPtr);
	(void)tableType;
	return hashTable[h];
}

#define LZ4_CLEAR_TABLE_THRESHOLD (LZ4_HASHTABLESIZE / 16)

LZ4_FORCE_INLINE void LZ4_prepareTable(LZ4_kstream_t *const cctx,
				       const int inputSize,
				       const tableType_t tableType)
{
	if ((tableType_t)cctx->tableType != clearedTable) {
		assert(inputSize >= 0);
		if ((tableType_t)cctx->tableType != tableType ||
		    ((tableType == byU16) &&
		     cctx->currentOffset + (unsigned)inputSize >= 0xFFFFU) ||
		    ((tableType == byU32) && cctx->currentOffset > 1 * GB) ||
		    tableType == byPtr ||
		    inputSize >= LZ4_CLEAR_TABLE_THRESHOLD) {
			MEM_INIT(cctx->hashTable, 0, LZ4_HASHTABLESIZE);
			cctx->currentOffset = 0;
			cctx->tableType = (U32)clearedTable;
		}
	}

	if (cctx->currentOffset != 0 && tableType == byU32)
		cctx->currentOffset += 64 * KB;

	cctx->dictCtx = NULL;
	cctx->dictionary = NULL;
	cctx->dictSize = 0;
}

LZ4_FORCE_INLINE int LZ4_match8(const void *p, const void *q)
{
	if (sizeof(reg_t) == 8)
		return LZ4_read_ARCH(p) == LZ4_read_ARCH(q);
	return (LZ4_read32(p) == LZ4_read32(q)) &&
	       (LZ4_read32((const BYTE *)p + 4) ==
		LZ4_read32((const BYTE *)q + 4));
}

typedef struct {
	const BYTE *ip;
	BYTE *op;
	U32 startIndex;
} LZ4_resume_t;

LZ4_FORCE_INLINE int LZ4_compress_generic_validated(
			LZ4_kstream_t *const cctx,
			const char *const source,
			char *const dest,
			const int inputSize,
			int *inputConsumed,
			const int maxOutputSize,
			const limitedOutput_directive outputDirective,
			const tableType_t tableType,
			const dict_directive dictDirective,
			const dictIssue_directive dictIssue,
			const int acceleration,
			LZ4_resume_t *const suspend,
			const LZ4_resume_t *const resume)
{
	int result;
	const BYTE *ip = resume ? resume->ip : (const BYTE *)source;
	U32 const startIndex = resume ? resume->startIndex : cctx->currentOffset;
	const BYTE *base = (const BYTE *)source - startIndex;
	const BYTE *lowLimit;
	const LZ4_kstream_t *dictCtx = (const LZ4_kstream_t *)cctx->dictCtx;
	const BYTE *const dictionary = dictDirective == usingDictCtx ?
		dictCtx->dictionary : cctx->dictionary;
	const U32 dictSize = dictDirective == usingDictCtx ?
		dictCtx->dictSize : cctx->dictSize;
	const U32 dictDelta = (dictDirective == usingDictCtx) ?
		startIndex - dictCtx->currentOffset : 0;
	int const maybe_extMem = (dictDirective == usingExtDict) ||
		(dictDirective == usingDictCtx);
	U32 const prefixIdxLimit = startIndex - dictSize;
	const BYTE *const dictEnd = dictionary ? dictionary + dictSize :
		dictionary;
	const BYTE *anchor = resume ? resume->ip : (const BYTE *)source;
	const BYTE *const iend = (const BYTE *)source + inputSize;
	const BYTE *mflimitPlusOne;
	const BYTE *matchlimit;
	const BYTE *seqLimit;
	const BYTE *dictBase = (dictionary == NULL) ? NULL :
		(dictDirective == usingDictCtx) ?
		dictionary + dictSize - dictCtx->currentOffset :
		dictionary + dictSize - startIndex;
	BYTE *op = resume ? resume->op : (BYTE *)dest;
	BYTE *const olimit = (BYTE *)dest + maxOutputSize;
	U32 offset = 0;
	U32 forwardH;
	const BYTE *filledIp = NULL;

	assert(ip != NULL);
	if (tableType == byU16)
		assert(inputSize < LZ4_64Klimit);
	if (tableType == byPtr)
		assert(dictDirective == noDict);
	if (outputDirective == fillOutput && maxOutputSize < 1)
		return 0;
	assert(acceleration >= 1);

	lowLimit = (const BYTE *)source -
		((dictDirective == withPrefix64k) && !resume ? dictSize : 0);

	if (!resume) {
		if (dictDirective == usingDictCtx) {
			cctx->dictCtx = NULL;
			cctx->dictSize = (U32)inputSize;
		} else {
			cctx->dictSize += (U32)inputSize;
		}
		cctx->currentOffset += (U32)inputSize;
		cctx->tableType = (U32)tableType;

		if (inputSize < LZ4_minLength)
			goto _last_literals;
	}
	mflimitPlusOne = iend - MFLIMIT + 1;
	matchlimit = iend - LASTLITERALS;
	seqLimit = mflimitPlusOne;
	if ((suspend != NULL) &&
	    (inputSize > LZ4_DISTANCE_MAX + MFLIMIT - 1))
		seqLimit = (const BYTE *)source + LZ4_DISTANCE_MAX;

	if (resume)
		goto _resume;

	LZ4_putPosition(ip, cctx->hashTable, tableType, base);
	ip++;
	forwardH = LZ4_hashPosition(ip, tableType);

	for (;;) {
		const BYTE *match;
		BYTE *token;

		if (tableType == byPtr) {
			const BYTE *forwardIp = ip;
			int step = 1;
			int searchMatchNb = acceleration << LZ4_skipTrigger;

			do {
				U32 const h = forwardH;

				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_skipTrigger);

				if (unlikely(forwardIp > mflimitPlusOne))
					goto _last_literals;
				assert(ip < mflimitPlusOne);

				match = LZ4_getPositionOnHash(h, cctx->hashTable,
							      tableType);
				forwardH = LZ4_hashPosition(forwardIp,
							    tableType);
				LZ4_putPositionOnHash(ip, h, cctx->hashTable,
						      tableType);
			} while ((match + LZ4_DISTANCE_MAX < ip) ||
				 (LZ4_read32(match) != LZ4_read32(ip)));
		} else {
			const BYTE *forwardIp = ip;
			int step = 1;
			int searchMatchNb = acceleration << LZ4_skipTrigger;

			do {
				U32 const h = forwardH;
				U32 const ipIndex = (U32)(forwardIp - base);
				U32 matchIndex = LZ4_getIndexOnHash(h,
						cctx->hashTable, tableType);

				assert(matchIndex <= ipIndex);
				assert(forwardIp - base < (ptrdiff_t)(2 * GB - 1));
				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_skipTrigger);

				if (unlikely(forwardIp > mflimitPlusOne))
					goto _last_literals;
				assert(ip < mflimitPlusOne);

				if (dictDirective == usingDictCtx) {
					if (matchIndex < startIndex) {
						assert(tableType == byU32);
						matchIndex = LZ4_getIndexOnHash(h,
							dictCtx->hashTable,
							byU32);
						match = dictBase + matchIndex;
						matchIndex += dictDelta;
						lowLimit = dictionary;
					} else {
						match = base + matchIndex;
						lowLimit = (const BYTE *)source;
					}
				} else if (dictDirective == usingExtDict) {
					ptrdiff_t const relIdx =
						(ptrdiff_t)matchIndex -
						(ptrdiff_t)startIndex;

					match = ((relIdx < 0) ? dictEnd :
						 (const BYTE *)source) + relIdx;
				} else {
					match = base + matchIndex;
				}
				forwardH = LZ4_hashPosition(forwardIp,
							    tableType);
				LZ4_putIndexOnHash(ipIndex, h, cctx->hashTable,
						   tableType);

				if ((dictIssue == dictSmall) &&
				    (matchIndex < prefixIdxLimit))
					continue;
				assert(matchIndex < ipIndex);
				assert((dictDirective != noDict) ||
				       (match >= (const BYTE *)source));
				if ((dictDirective != noDict) &&
				    ((tableType != byU16) ||
				     (LZ4_DISTANCE_MAX <
				      LZ4_DISTANCE_ABSOLUTE_MAX)) &&
				    (matchIndex + LZ4_DISTANCE_MAX < ipIndex))
					continue;

				if (LZ4_read32(match) == LZ4_read32(ip)) {
					if ((dictDirective == noDict) &&
					    ((tableType != byU16) ||
					     (LZ4_DISTANCE_MAX <
					      LZ4_DISTANCE_ABSOLUTE_MAX)) &&
					    (matchIndex + LZ4_DISTANCE_MAX <
					     ipIndex))
						continue;
					assert((ipIndex - matchIndex) <=
					       LZ4_DISTANCE_MAX);
					if (maybe_extMem)
						offset = ipIndex - matchIndex;
					if (dictDirective == usingExtDict)
						lowLimit =
							(matchIndex < startIndex) ?
							dictionary :
							(const BYTE *)source;
					break;
				}
			} while (1);
		}

		filledIp = ip;
		assert(ip > anchor);
		if ((match > lowLimit) && unlikely(ip[-1] == match[-1])) {
			do {
				ip--;
				match--;
			} while (((ip > anchor) & (match > lowLimit)) &&
				 (unlikely(ip[-1] == match[-1])));
		}

		{
			unsigned const litLength = (unsigned)(ip - anchor);

			token = op++;
			if ((outputDirective == limitedOutput) &&
			    (unlikely(op + litLength +
				      (2 + 1 + LASTLITERALS) +
				      (litLength / 255) > olimit)))
				return 0;
			if ((outputDirective == fillOutput) &&
			    (unlikely(op + (litLength + 240) / 255 +
				      litLength + 2 + 1 + MFLIMIT - MINMATCH >
				      olimit))) {
				op--;
				goto _last_literals;
			}
			if (litLength >= RUN_MASK) {
				unsigned int len = litLength - RUN_MASK;

				*token = (RUN_MASK << ML_BITS);
				for (; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = (BYTE)len;
			} else {
				*token = (BYTE)(litLength << ML_BITS);
			}

			LZ4_wildCopy8(op, anchor, op + litLength);
			op += litLength;
		}

_next_match:
		if ((outputDirective == fillOutput) &&
		    (op + 2 + 1 + MFLIMIT - MINMATCH > olimit)) {
			op = token;
			goto _last_literals;
		}

		if (maybe_extMem) {
			assert(offset <= LZ4_DISTANCE_MAX && offset > 0);
			LZ4_writeLE16(op, (U16)offset);
			op += 2;
		} else {
			assert(ip - match <= LZ4_DISTANCE_MAX);
			LZ4_writeLE16(op, (U16)(ip - match));
			op += 2;
		}

		{
			unsigned int matchCode;

			if ((dictDirective == usingExtDict ||
			     dictDirective == usingDictCtx) &&
			    (lowLimit == dictionary)) {
				const BYTE *limit = ip + (dictEnd - match);

				assert(dictEnd > match);
				if (limit > matchlimit)
					limit = matchlimit;
				matchCode = LZ4_count(ip + MINMATCH,
						      match + MINMATCH, limit);
				ip += (size_t)matchCode + MINMATCH;
				if (ip == limit) {
					unsigned const more = LZ4_count(limit,
						(const BYTE *)source,
						matchlimit);

					matchCode += more;
					ip += more;
				}
			} else {
				matchCode = LZ4_count(ip + MINMATCH,
						      match + MINMATCH,
						      matchlimit);
				ip += (size_t)matchCode + MINMATCH;
			}

			if ((outputDirective) &&
			    (unlikely(op + (1 + LASTLITERALS) +
				      (matchCode + 240) / 255 > olimit))) {
				if (outputDirective == fillOutput) {
					U32 newMatchCode = 15 - 1 +
						((U32)(olimit - op) - 1 -
						 LASTLITERALS) * 255;

					ip -= matchCode - newMatchCode;
					assert(newMatchCode < matchCode);
					matchCode = newMatchCode;
					if (unlikely(ip <= filledIp)) {
						const BYTE *ptr;

						for (ptr = ip; ptr <= filledIp;
						     ++ptr) {
							U32 const h =
								LZ4_hashPosition(
									ptr,
									tableType);

							LZ4_clearHash(h,
								cctx->hashTable,
								tableType);
						}
					}
				} else {
					assert(outputDirective ==
					       limitedOutput);
					return 0;
				}
			}
			if (matchCode >= ML_MASK) {
				*token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4_write32(op, 0xFFFFFFFF);
				while (matchCode >= 4 * 255) {
					op += 4;
					LZ4_write32(op, 0xFFFFFFFF);
					matchCode -= 4 * 255;
				}
				op += matchCode / 255;
				*op++ = (BYTE)(matchCode % 255);
			} else {
				*token += (BYTE)(matchCode);
			}
		}
		assert(!(outputDirective == fillOutput &&
			 op + 1 + LASTLITERALS > olimit));

		anchor = ip;

		if (ip >= seqLimit) {
			if ((suspend == NULL) || (ip >= mflimitPlusOne))
				break;
			suspend->ip = ip;
			suspend->op = op;
			suspend->startIndex = startIndex;
			return 0;
		}

_resume:
		if (tableType == byPtr) {
			U32 const hPrev = LZ4_hashPosition(ip - 1, tableType);
			const BYTE *const prev = LZ4_getPositionOnHash(hPrev,
						cctx->hashTable, tableType);
			U32 const h = LZ4_hashPosition(ip, tableType);

			LZ4_putPositionOnHash(ip - 1, hPrev, cctx->hashTable,
					      tableType);
			match = LZ4_getPositionOnHash(h, cctx->hashTable,
						      tableType);
			if ((prev + 1 + LZ4_DISTANCE_MAX >= ip) &&
			    LZ4_match8(prev + 1, ip))
				match = prev + 1;
			LZ4_putPositionOnHash(ip, h, cctx->hashTable,
					      tableType);
			if ((match + LZ4_DISTANCE_MAX >= ip) &&
			    (LZ4_read32(match) == LZ4_read32(ip))) {
				token = op++;
				*token = 0;
				goto _next_match;
			}
		} else {
			U32 const h = LZ4_hashPosition(ip, tableType);
			U32 const ipIndex = (U32)(ip - base);
			U32 const hPrev = LZ4_hashPosition(ip - 1, tableType);
			U32 prevIndex = LZ4_getIndexOnHash(hPrev,
						cctx->hashTable, tableType);
			U32 matchIndex;

			LZ4_putIndexOnHash(ipIndex - 1, hPrev, cctx->hashTable,
					   tableType);
			if ((dictDirective == usingDictCtx) &&
			    (prevIndex < startIndex))
				prevIndex = LZ4_getIndexOnHash(hPrev,
					dictCtx->hashTable, byU32) + dictDelta;
			{
				int const usedTable = (dictDirective == noDict) &&
					(dictIssue == dictSmall);
				U32 const ctxIndex =
					((usedTable && (prevIndex < startIndex)) ?
					 startIndex : prevIndex) + 1;
				int const inDict =
					(dictDirective == usingExtDict ||
					 dictDirective == usingDictCtx) &&
					(ctxIndex < startIndex);

				if (((dictIssue == dictSmall) ?
				     (ctxIndex >= prefixIdxLimit) : 1) &&
				    (((tableType == byU16) &&
				      (LZ4_DISTANCE_MAX ==
				       LZ4_DISTANCE_ABSOLUTE_MAX)) ? 1 :
				     (ctxIndex + LZ4_DISTANCE_MAX >= ipIndex)) &&
				    (!inDict || (ctxIndex + 8 <= startIndex)) &&
				    LZ4_match8(inDict ?
					       dictBase + (ctxIndex - dictDelta) :
					       base + ctxIndex, ip)) {
					matchIndex = ctxIndex;
				} else {
					matchIndex = LZ4_getIndexOnHash(h,
						cctx->hashTable, tableType);
					if ((dictDirective == usingDictCtx) &&
					    (matchIndex < startIndex))
						matchIndex =
							LZ4_getIndexOnHash(h,
								dictCtx->hashTable,
								byU32) +
							dictDelta;
				}
			}
			assert(matchIndex < ipIndex);
			if (dictDirective == usingExtDict) {
				ptrdiff_t const relIdx =
					(ptrdiff_t)matchIndex -
					(ptrdiff_t)startIndex;

				match = ((relIdx < 0) ? dictEnd :
					 (const BYTE *)source) + relIdx;
			} else if ((dictDirective == usingDictCtx) &&
				   (matchIndex < startIndex)) {
				match = dictBase + (matchIndex - dictDelta);
				lowLimit = dictionary;
			} else {
				match = base + matchIndex;
				if (dictDirective == usingDictCtx)
					lowLimit = (const BYTE *)source;
			}
			LZ4_putIndexOnHash(ipIndex, h, cctx->hashTable,
					   tableType);
			assert(matchIndex < ipIndex);
			if (((dictIssue == dictSmall) ?
			     (matchIndex >= prefixIdxLimit) : 1) &&
			    (((tableType == byU16) &&
			      (LZ4_DISTANCE_MAX ==
			       LZ4_DISTANCE_ABSOLUTE_MAX)) ? 1 :
			     (matchIndex + LZ4_DISTANCE_MAX >= ipIndex)) &&
			    (LZ4_read32(match) == LZ4_read32(ip))) {
				token = op++;
				*token = 0;
				if (maybe_extMem)
					offset = ipIndex - matchIndex;
				if (dictDirective == usingExtDict)
					lowLimit = (matchIndex < startIndex) ?
						dictionary : (const BYTE *)source;
				goto _next_match;
			}
		}

		forwardH = LZ4_hashPosition(++ip, tableType);
	}

_last_literals:
	{
		size_t lastRun = (size_t)(iend - anchor);

		if ((outputDirective) &&
		    (op + lastRun + 1 +
		     ((lastRun + 255 - RUN_MASK) / 255) > olimit)) {
			if (outputDirective == fillOutput) {
				assert(olimit >= op);
				lastRun = (size_t)(olimit - op) - 1;
				lastRun -= (lastRun + 256 - RUN_MASK) / 256;
			} else {
				assert(outputDirective == limitedOutput);
				return 0;
			}
		}
		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;

			*op++ = RUN_MASK << ML_BITS;
			for (; accumulator >= 255; accumulator -= 255)
				*op++ = 255;
			*op++ = (BYTE)accumulator;
		} else {
			*op++ = (BYTE)(lastRun << ML_BITS);
		}
		LZ4_memcpy(op, anchor, lastRun);
		ip = anchor + lastRun;
		op += lastRun;
	}

	if (outputDirective == fillOutput)
		*inputConsumed = (int)(((const char *)ip) - source);
	result = (int)(((char *)op) - dest);
	assert(result > 0);
	return result;
}

FORCE_NOINLINE int LZ4_compress_resumeWithPrefix(LZ4_kstream_t *cctx,
			const char *src, char *dst, int srcSize,
			int dstCapacity, int acceleration,
			const LZ4_resume_t *state)
{
	return LZ4_compress_generic_validated(cctx, src, dst, srcSize, NULL,
			dstCapacity, limitedOutput, byU32, withPrefix64k,
			noDictIssue, acceleration, NULL, state);
}

LZ4_FORCE_INLINE int LZ4_compress_generic(LZ4_kstream_t *const cctx,
			const char *const src, char *const dst,
			const int srcSize, int *inputConsumed,
			const int dstCapacity,
			const limitedOutput_directive outputDirective,
			const tableType_t tableType,
			const dict_directive dictDirective,
			const dictIssue_directive dictIssue,
			const int acceleration)
{
	if ((U32)srcSize > (U32)LZ4_MAX_INPUT_SIZE)
		return 0;
	if (srcSize == 0) {
		if (outputDirective != notLimited && dstCapacity <= 0)
			return 0;
		assert(outputDirective == notLimited || dstCapacity >= 1);
		assert(dst != NULL);
		dst[0] = 0;
		if (outputDirective == fillOutput) {
			assert(inputConsumed != NULL);
			*inputConsumed = 0;
		}
		return 1;
	}
	assert(src != NULL);

	if ((dictDirective == usingExtDict) && (dictIssue == noDictIssue)) {
		LZ4_resume_t state;
		int result;

		assert(tableType == byU32);
		assert(outputDirective != fillOutput);
		state.ip = NULL;
		result = LZ4_compress_generic_validated(cctx, src, dst, srcSize,
				inputConsumed, dstCapacity, outputDirective,
				tableType, usingExtDict, noDictIssue, acceleration,
				&state, NULL);
		if (state.ip == NULL)
			return result;
		return LZ4_compress_resumeWithPrefix(cctx, src, dst, srcSize,
				(outputDirective == notLimited) ?
				LZ4_COMPRESSBOUND(srcSize) : dstCapacity,
				acceleration, &state);
	}

	return LZ4_compress_generic_validated(cctx, src, dst, srcSize,
			inputConsumed, dstCapacity, outputDirective,
			tableType, dictDirective, dictIssue, acceleration,
			NULL, NULL);
}

static int LZ4_clampAcceleration(int acceleration)
{
	if (acceleration < 1)
		return LZ4_ACCELERATION_DEFAULT;
	if (acceleration > LZ4_ACCELERATION_MAX)
		return LZ4_ACCELERATION_MAX;
	return acceleration;
}

static tableType_t LZ4_largeInputTableType(const char *src)
{
	(void)src;
	return ((sizeof(void *) == 4) && ((uptrval)src > LZ4_DISTANCE_MAX)) ?
		byPtr : byU32;
}

LZ4_FORCE_INLINE int LZ4_compress_fastReset_generic(
			LZ4_kstream_t *const ctx,
			const char *const src, char *const dst,
			const int srcSize, const int dstCapacity,
			const limitedOutput_directive outputDirective,
			const int acceleration)
{
	if (srcSize < LZ4_64Klimit) {
		LZ4_prepareTable(ctx, srcSize, byU16);
		if (ctx->currentOffset)
			return LZ4_compress_generic(ctx, src, dst, srcSize,
					NULL, dstCapacity, outputDirective,
					byU16, noDict, dictSmall,
					acceleration);
		return LZ4_compress_generic(ctx, src, dst, srcSize, NULL,
				dstCapacity, outputDirective, byU16, noDict,
				noDictIssue, acceleration);
	} else {
		const tableType_t tableType = LZ4_largeInputTableType(src);

		LZ4_prepareTable(ctx, srcSize, tableType);
		return LZ4_compress_generic(ctx, src, dst, srcSize, NULL,
				dstCapacity, outputDirective, tableType, noDict,
				noDictIssue, acceleration);
	}
}

static LZ4_stream_t *LZ4_initStream(void *buffer, size_t size)
{
	if (!buffer || size < sizeof(LZ4_stream_t))
		return NULL;
	if (!LZ4_isAligned(buffer, sizeof(reg_t)))
		return NULL;
	MEM_INIT(buffer, 0, sizeof(LZ4_kstream_t));
	return (LZ4_stream_t *)buffer;
}

static int LZ4_compress_fast_extState_fastReset(void *state,
			const char *src, char *dst, int srcSize,
			int dstCapacity, int acceleration)
{
	LZ4_kstream_t *const ctx = LZ4_STREAM_STATE(state);

	acceleration = LZ4_clampAcceleration(acceleration);

	if (dstCapacity >= LZ4_compressBound(srcSize))
		return LZ4_compress_fastReset_generic(ctx, src, dst, srcSize, 0,
				notLimited, acceleration);
	return LZ4_compress_fastReset_generic(ctx, src, dst, srcSize,
			dstCapacity, limitedOutput, acceleration);
}

static int LZ4_compress_fast_extState(void *state, const char *source,
			char *dest, int inputSize, int maxOutputSize,
			int acceleration)
{
	LZ4_stream_t *const s = LZ4_initStream(state,
					       sizeof(LZ4_stream_t));

	assert(s != NULL);
	if (!s)
		return 0;
	return LZ4_compress_fast_extState_fastReset(s, source, dest,
			inputSize, maxOutputSize, acceleration);
}

int LZ4_compress_fast(const char *source, char *dest, int inputSize,
		      int maxOutputSize, int acceleration, void *wrkmem)
{
	return LZ4_compress_fast_extState(wrkmem, source, dest, inputSize,
			maxOutputSize, acceleration);
}
EXPORT_SYMBOL(LZ4_compress_fast);

int LZ4_compress_default(const char *source, char *dest, int inputSize,
			 int maxOutputSize, void *wrkmem)
{
	return LZ4_compress_fast_extState(wrkmem, source, dest, inputSize,
			maxOutputSize, 1);
}
EXPORT_SYMBOL(LZ4_compress_default);

static int LZ4_compress_destSize_extState_internal(LZ4_stream_t *state,
			const char *src, char *dst, int *srcSizePtr,
			int targetDstSize, int acceleration)
{
	void *const s = LZ4_initStream(state, sizeof(*state));

	assert(s != NULL);
	(void)s;
	acceleration = LZ4_clampAcceleration(acceleration);

	if (targetDstSize >= LZ4_compressBound(*srcSizePtr)) {
		return LZ4_compress_fast_extState_fastReset(state, src, dst,
				*srcSizePtr, targetDstSize, acceleration);
	} else {
		LZ4_kstream_t *const ctx = LZ4_STREAM_STATE(state);

		if (*srcSizePtr < LZ4_64Klimit)
			return LZ4_compress_generic(ctx, src, dst,
					*srcSizePtr, srcSizePtr, targetDstSize,
					fillOutput, byU16, noDict, noDictIssue,
					acceleration);
		return LZ4_compress_generic(ctx, src, dst, *srcSizePtr,
				srcSizePtr, targetDstSize, fillOutput,
				LZ4_largeInputTableType(src), noDict,
				noDictIssue, acceleration);
	}
}

static int LZ4_compress_destSize_extState(void *state, const char *src,
			char *dst, int *srcSizePtr, int targetDstSize,
			int acceleration)
{
	int const r = LZ4_compress_destSize_extState_internal(
			(LZ4_stream_t *)state, src, dst, srcSizePtr,
			targetDstSize, acceleration);

	LZ4_initStream(state, sizeof(LZ4_stream_t));
	return r;
}

int LZ4_compress_destSize(const char *source, char *dest, int *sourceSizePtr,
			  int targetDestSize, void *wrkmem)
{
	return LZ4_compress_destSize_extState(wrkmem, source, dest,
			sourceSizePtr, targetDestSize, 1);
}
EXPORT_SYMBOL(LZ4_compress_destSize);

void LZ4_resetStream(LZ4_stream_t *LZ4_stream)
{
	MEM_INIT(LZ4_stream, 0, sizeof(LZ4_kstream_t));
}

static void LZ4_loadDict_internal(LZ4_stream_t *LZ4_dict,
				  const char *dictionary, int dictSize)
{
	LZ4_kstream_t *const dict = LZ4_STREAM_STATE(LZ4_dict);
	const tableType_t tableType = byU32;
	const BYTE *p = (const BYTE *)dictionary;
	const BYTE *const dictEnd = p + dictSize;
	U32 idx32;

	LZ4_resetStream(LZ4_dict);

	dict->currentOffset += 64 * KB;
	dict->tableType = (U32)tableType;

	if (dictSize < (int)HASH_UNIT)
		return;

	if ((dictEnd - p) > 64 * KB)
		p = dictEnd - 64 * KB;
	dict->dictionary = p;
	dict->dictSize = (U32)(dictEnd - p);
	idx32 = dict->currentOffset - dict->dictSize;

	while (p <= dictEnd - HASH_UNIT) {
		U32 const h = LZ4_hashPosition(p, tableType);

		LZ4_putIndexOnHash(idx32, h, dict->hashTable, tableType);
		p += 3;
		idx32 += 3;
	}
}

int LZ4_loadDict(LZ4_stream_t *LZ4_dict, const char *dictionary, int dictSize)
{
	LZ4_loadDict_internal(LZ4_dict, dictionary, dictSize);
	return LZ4_STREAM_STATE(LZ4_dict)->dictSize;
}
EXPORT_SYMBOL(LZ4_loadDict);

static void LZ4_renormDictT(LZ4_kstream_t *LZ4_dict, int nextSize)
{
	assert(nextSize >= 0);
	if (LZ4_dict->currentOffset + (unsigned)nextSize > 0x80000000) {
		U32 const delta = LZ4_dict->currentOffset - 64 * KB;
		const BYTE *dictEnd = LZ4_dict->dictionary + LZ4_dict->dictSize;
		int i;

		for (i = 0; i < LZ4_HASH_SIZE_U32; i++) {
			if (LZ4_dict->hashTable[i] < delta)
				LZ4_dict->hashTable[i] = 0;
			else
				LZ4_dict->hashTable[i] -= delta;
		}
		LZ4_dict->currentOffset = 64 * KB;
		if (LZ4_dict->dictSize > 64 * KB)
			LZ4_dict->dictSize = 64 * KB;
		LZ4_dict->dictionary = dictEnd - LZ4_dict->dictSize;
	}
}

static int LZ4_isDictSmall(const LZ4_kstream_t *streamPtr)
{
	return (streamPtr->dictSize < 64 * KB) &&
		(streamPtr->dictSize < streamPtr->currentOffset);
}

#define LZ4_DICTCTX_COPY_THRESHOLD \
	((LZ4_HASHTABLESIZE / 8 < 4 * KB) ? (LZ4_HASHTABLESIZE / 8) : 4 * KB)

int LZ4_compress_fast_continue(LZ4_stream_t *LZ4_stream,
			       const char *source, char *dest,
			       int inputSize, int maxOutputSize,
			       int acceleration)
{
	const tableType_t tableType = byU32;
	LZ4_kstream_t *const streamPtr = LZ4_STREAM_STATE(LZ4_stream);
	const char *dictEnd = streamPtr->dictSize ?
		(const char *)streamPtr->dictionary + streamPtr->dictSize : NULL;

	if ((streamPtr->dictSize == 0) && (!streamPtr->dictCtx) &&
	    (inputSize >= LZ4_CLEAR_TABLE_THRESHOLD))
		LZ4_prepareTable(streamPtr, inputSize, byU32);

	LZ4_renormDictT(streamPtr, inputSize);
	acceleration = LZ4_clampAcceleration(acceleration);

	if ((streamPtr->dictSize < 4) && (dictEnd != source) &&
	    (inputSize > 0) && (!streamPtr->dictCtx)) {
		streamPtr->dictSize = 0;
		streamPtr->dictionary = (const BYTE *)source;
		dictEnd = source;
	}

	{
		const char *const sourceEnd = source + inputSize;

		if ((sourceEnd > (const char *)streamPtr->dictionary) &&
		    (source < dictEnd)) {
			streamPtr->dictSize = (sourceEnd < dictEnd) ?
				(U32)(dictEnd - sourceEnd) : 0;
			if (streamPtr->dictSize > 64 * KB)
				streamPtr->dictSize = 64 * KB;
			if (streamPtr->dictSize < 4)
				streamPtr->dictSize = 0;
			streamPtr->dictionary =
				(const BYTE *)dictEnd - streamPtr->dictSize;
		}
	}

	if (dictEnd == source) {
		if (LZ4_isDictSmall(streamPtr))
			return LZ4_compress_generic(streamPtr, source, dest,
					inputSize, NULL, maxOutputSize,
					limitedOutput, tableType,
					withPrefix64k, dictSmall, acceleration);
		return LZ4_compress_generic(streamPtr, source, dest, inputSize,
				NULL, maxOutputSize, limitedOutput, tableType,
				withPrefix64k, noDictIssue, acceleration);
	}

	{
		int result;

		if (streamPtr->dictCtx) {
			if (inputSize >= LZ4_DICTCTX_COPY_THRESHOLD) {
				LZ4_memcpy(streamPtr, streamPtr->dictCtx,
					   sizeof(*streamPtr));
				result = LZ4_compress_generic(streamPtr, source,
						dest, inputSize, NULL,
						maxOutputSize, limitedOutput,
						tableType, usingExtDict,
						noDictIssue, acceleration);
			} else {
				result = LZ4_compress_generic(streamPtr, source,
						dest, inputSize, NULL,
						maxOutputSize, limitedOutput,
						tableType, usingDictCtx,
						noDictIssue, acceleration);
			}
		} else {
			if (LZ4_isDictSmall(streamPtr))
				result = LZ4_compress_generic(streamPtr,
						source, dest, inputSize, NULL,
						maxOutputSize, limitedOutput,
						tableType, usingExtDict,
						dictSmall, acceleration);
			else
				result = LZ4_compress_generic(streamPtr,
						source, dest, inputSize, NULL,
						maxOutputSize, limitedOutput,
						tableType, usingExtDict,
						noDictIssue, acceleration);
		}
		streamPtr->dictionary = (const BYTE *)source;
		streamPtr->dictSize = (U32)inputSize;
		return result;
	}
}
EXPORT_SYMBOL(LZ4_compress_fast_continue);

int LZ4_saveDict(LZ4_stream_t *LZ4_dict, char *safeBuffer, int dictSize)
{
	LZ4_kstream_t *const dict = LZ4_STREAM_STATE(LZ4_dict);

	if (dictSize < 0)
		dictSize = 0;
	if ((U32)dictSize > 64 * KB)
		dictSize = 64 * KB;
	if ((U32)dictSize > dict->dictSize)
		dictSize = (int)dict->dictSize;

	if (safeBuffer == NULL)
		assert(dictSize == 0);
	if (dictSize > 0) {
		const BYTE *const previousDictEnd =
			dict->dictionary + dict->dictSize;

		assert(dict->dictionary);
		LZ4_memmove(safeBuffer, previousDictEnd - dictSize,
			    (size_t)dictSize);
	}

	dict->dictionary = (const BYTE *)safeBuffer;
	dict->dictSize = (U32)dictSize;

	return dictSize;
}
EXPORT_SYMBOL(LZ4_saveDict);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LZ4 compressor");
