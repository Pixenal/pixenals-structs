/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <math.h>

#include <pixenals_structs.h>

static
I32 getNearbyPrime(I32 num) {
	I32 primes[] = {
		1,
		3,
		5,
		11,
		17,
		37,
		67,
		131,
		257,
		521,
		1031,
		2053,
		4099,
		8209,
		16411,
		32771,
		65537,
		131101,
		262147,
		524309,
		1048583,
		2097169,
		4194319,
		8388617,
		16777259,
		33554467,
		67108879,
		134217757,
		268435459
	};
	F32 exp = log2f((F32)num);
	I32 expRound = (I32)roundf(exp);
	PIX_ERR_ASSERT("a value this high shouldn't've been passed", expRound <= 28);
	return primes[expRound];
}

void pixuctHTableInit(
	const PixalcFPtrs *pAlloc,
	PixuctHTable *pHandle,
	I32 targetSize,
	PixtyI32Arr allocTypeSizes,
	void *pUserData
) {
	PIX_ERR_ASSERT("", targetSize > 0);
	I32 size = getNearbyPrime(targetSize);
	*pHandle = (PixuctHTable){
		.pAlloc = pAlloc,
		.pUserData = pUserData,
		.size = size,
		.pTable = pAlloc->fpCalloc(size, sizeof(PixuctHTableBucket))
	};
	PIX_ERR_ASSERT(
		"",
		allocTypeSizes.count > 0 && allocTypeSizes.count <= PIX_HTABLE_ALLOC_HANDLES_MAX
	);
	I32 allocInitSize = size / allocTypeSizes.count / 2 + 1;
	for (I32 i = 0; i < allocTypeSizes.count; ++i) {
		pixalcLinAllocInit(
			pAlloc,
			pHandle->allocHandles + i,
			allocTypeSizes.pArr[i],
			allocInitSize,
			true
		);
	}
}

void pixuctHTableDestroy(PixuctHTable *pHandle) {
	if (pHandle->pTable) {
		PIX_ERR_ASSERT("", pHandle->size);
		pHandle->pAlloc->fpFree(pHandle->pTable);
	}
	PIX_ERR_ASSERT(
		"at least 1 lin alloc handle should have been initialized",
		pHandle->allocHandles[0].valid
	);
	for (I32 i = 0; i < PIX_HTABLE_ALLOC_HANDLES_MAX; ++i) {
		if (pHandle->allocHandles[i].valid) {
			pixalcLinAllocDestroy(pHandle->allocHandles + i);
		}
	}
	*pHandle = (PixuctHTable) {0};
}

PixalcLinAlloc *pixuctHTableAllocGet(PixuctHTable *pHandle, I32 idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < PIX_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->allocHandles + idx;
}

const PixalcLinAlloc *pixuctHTableAllocGetConst(const PixuctHTable *pHandle, I32 idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < PIX_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->allocHandles + idx;
}

PixuctHTableBucket *pixuctHTableBucketGet(PixuctHTable *pHandle, PixuctKey key) {
	U64 hash = stucFnvHash((U8 *)key.pKey, key.size, pHandle->size);
	return pHandle->pTable + hash;
}
