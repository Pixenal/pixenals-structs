/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <string.h>

#include "../../pixenals-alloc-utils/include/pixenals_alloc_utils.h"
#include "../../pixenals-error-utils/include/pixenals_error_utils.h"

#ifndef PIX_FORCE_INLINE
#ifdef NDEBUG
#ifdef WIN32
#define PIX_FORCE_INLINE __forceinline
#else
#define PIX_FORCE_INLINE __attribute__((always_inline)) static inline
#endif
#else
#define PIX_FORCE_INLINE static inline
#endif
#endif

typedef int32_t I32;
typedef int64_t I64;
typedef uint8_t U8;
typedef uint32_t U32;
typedef uint64_t U64;
typedef float F32;

typedef struct PixuctHTableEntryCore {
	struct PixuctHTableEntryCore *pNext;
} PixuctHTableEntryCore;

typedef struct PixuctHTableBucket {
	PixuctHTableEntryCore *pList;
} PixuctHTableBucket;

#define PIX_HTABLE_ALLOC_HANDLES_MAX 2


typedef struct PixuctHTableMemBuckets {
	PixuctHTableBucket *pArr;
	I32 size;
} PixuctHTableMemBuckets;

typedef struct PixuctHTableMemEntries {
	PixalcLinAlloc pArr[PIX_HTABLE_ALLOC_HANDLES_MAX];
	I32 count;
} PixuctHTableMemEntries;

typedef struct PixuctHTableMem {
	PixuctHTableMemBuckets buckets;
	PixuctHTableMemEntries entries;
} PixuctHTableMem;

typedef struct PixuctHTable {
	const PixalcFPtrs *pAlloc;
	PixuctHTableMem *pMem;
	PixalcLinAlloc allocHandles[PIX_HTABLE_ALLOC_HANDLES_MAX];
	bool linAlc[PIX_HTABLE_ALLOC_HANDLES_MAX];
	PixuctHTableBucket *pTable;
	void *pUserData;
	I32 size;
} PixuctHTable;

typedef struct PixuctKey {
	const void *pKey;
	I32 size;
} PixuctKey;

typedef enum SearchResult {
	PIX_SEARCH_FOUND,
	PIX_SEARCH_NOT_FOUND,
	PIX_SEARCH_ADDED
} SearchResult;

static inline
U32 stucFnvHash(const U8 *value, I32 valueSize, U32 size) {
	PIX_ERR_ASSERT("", value && valueSize > 0 && size > 0);
	U32 hash = 2166136261;
	for (I32 i = 0; i < valueSize; ++i) {
		hash ^= value[i];
		hash *= 16777619;
	}
	hash %= size;
	PIX_ERR_ASSERT("", hash >= 0);
	return hash;
}

static inline
PixalcLinAlloc *pixuctHTableAllocGet(PixuctHTable *pHandle, int32_t idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < PIX_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->linAlc[idx] ? pHandle->pMem->entries.pArr + idx : pHandle->allocHandles + idx;
}

static inline
const PixalcLinAlloc *pixuctHTableAllocGetConst(const PixuctHTable *pHandle, int32_t idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < PIX_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->linAlc[idx] ? pHandle->pMem->entries.pArr + idx : pHandle->allocHandles + idx;
}

void pixuctHTableInit(
	const PixalcFPtrs *pAlloc,
	PixuctHTable *pHandle,
	I32 targetSize,
	PixtyI32Arr allocTypeSizes,
	PixuctHTableMem *pMem,
	void *pUserData,
	bool zeroOnClear
);
void pixuctHTableDestroy(PixuctHTable *pHandle);

const PixalcLinAlloc *pixuctHTableAllocGetConst(const PixuctHTable *pHandle, I32 idx);
PixuctHTableBucket *pixuctHTableBucketGet(PixuctHTable *pHandle, PixuctKey key);
PIX_FORCE_INLINE
SearchResult pixuctHTableGet(
	PixuctHTable *pHandle,
	I32 alloc,
	const void *pKeyData,
	void **ppEntry,
	bool addEntry,
	void *pInitInfo,
	PixuctKey (* fpMakeKey)(const void *),
	bool (* fpAddPredicate)(const void *, const void *, const void *),
	//  is a callback needed for this?
	//v maybe just return the entry & the user can init with that v
	void (* fpInitEntry)(void *, PixuctHTableEntryCore *, const void *, void *, I32),
	bool (* fpCompareEntry)(const PixuctHTableEntryCore *, const void *, const void *)
) {
	PIX_ERR_ASSERT("", pHandle->pTable && pHandle->size);
	PixalcLinAlloc *pLinAlloc = pixuctHTableAllocGet(pHandle, alloc);
	PIX_ERR_ASSERT(
		"",
		alloc < PIX_HTABLE_ALLOC_HANDLES_MAX && pLinAlloc && pLinAlloc->valid
	);
	PIX_ERR_ASSERT("", (!addEntry || fpInitEntry) && fpCompareEntry);
	PixuctKey key = fpMakeKey(pKeyData);
	PIX_ERR_ASSERT("invalid key", key.size > 0);
	PixuctHTableBucket *pBucket = pixuctHTableBucketGet(pHandle, key);
	if (!pBucket->pList) {
		if (!addEntry ||
			fpAddPredicate && !fpAddPredicate(pHandle->pUserData, pKeyData, pInitInfo)
		) {
			return PIX_SEARCH_NOT_FOUND;
		}
		I32 linIdx = pixalcLinAlloc(pLinAlloc, (void **)&pBucket->pList, 1);
		*pBucket->pList = (PixuctHTableEntryCore){0};
		fpInitEntry(pHandle->pUserData, pBucket->pList, pKeyData, pInitInfo, linIdx);
		PIX_ERR_ASSERT("", pBucket->pList);
		if (ppEntry) {
			*ppEntry = pBucket->pList;
		}
		return PIX_SEARCH_ADDED;
	}
	PixuctHTableEntryCore *pEntry = pBucket->pList;
	do {
		if (fpCompareEntry(pEntry, pKeyData, pInitInfo)) {
			PIX_ERR_ASSERT("", pEntry);
			if (ppEntry) {
				*ppEntry = pEntry;
			}
			return PIX_SEARCH_FOUND;
		}
		if (!pEntry->pNext) {
			if (!addEntry ||
				fpAddPredicate && !fpAddPredicate(pHandle->pUserData, pKeyData, pInitInfo)
			) {
				return PIX_SEARCH_NOT_FOUND;
			}
			I32 linIdx = pixalcLinAlloc(pLinAlloc, (void **)&pEntry->pNext, 1);
			*pEntry->pNext = (PixuctHTableEntryCore){0};
			fpInitEntry(pHandle->pUserData, pEntry->pNext, pKeyData, pInitInfo, linIdx);
			PIX_ERR_ASSERT("", pEntry->pNext);
			if (ppEntry) {
				*ppEntry = pEntry->pNext;
			}
			return PIX_SEARCH_ADDED;
		}
	} while((pEntry = pEntry->pNext));
	return PIX_SEARCH_NOT_FOUND;
}

PIX_FORCE_INLINE
void pixuctHTableRemove(
	PixuctHTable *pHandle,
	I32 alloc,
	const void *pKeyData,
	PixuctKey (* fpMakeKey)(const void *),
	bool (* fpCompareEntry)(const PixuctHTableEntryCore *, const void *, const void *),
	void (* fpClearEntry)(void *, PixuctHTableEntryCore *, const void *)
) {
	PIX_ERR_ASSERT("", pHandle->pTable && pHandle->size);
	PixalcLinAlloc *pLinAlloc = pixuctHTableAllocGet(pHandle, alloc);
	PIX_ERR_ASSERT(
		"",
		alloc < PIX_HTABLE_ALLOC_HANDLES_MAX && pLinAlloc && pLinAlloc->valid
	);
	PIX_ERR_ASSERT("", fpMakeKey && fpCompareEntry);
	PixuctHTableBucket *pBucket = pixuctHTableBucketGet(pHandle, fpMakeKey(pKeyData));
	PIX_ERR_ASSERT("unable to find specified entry", pBucket->pList)
	PixuctHTableEntryCore *pEntry = pBucket->pList;
	PixuctHTableEntryCore *pPrev = NULL;
	do {
		if (fpCompareEntry(pEntry, pKeyData, NULL)) {
			break;
		}
		PIX_ERR_ASSERT("unable to find specified entry", !pEntry->pNext);
	} while(pPrev = pEntry, pEntry = pEntry->pNext);
	if (pPrev) {
		pPrev->pNext = pEntry->pNext;
	}
	else {
		pBucket->pList = pEntry->pNext;
	}
	pEntry->pNext = NULL;
	if (fpClearEntry) {
		fpClearEntry(pHandle->pUserData, pEntry, pKeyData);
	}
	pixalcLinAllocRegionClear(pLinAlloc, pEntry, 1);
}

PIX_FORCE_INLINE
SearchResult pixuctHTableGetConst(
	const PixuctHTable *pHandle,
	I32 alloc,
	const void *pKeyData,
	const void **ppEntry,
	PixuctKey (* fpMakeKey)(const void *),
	bool (* fpCompareEntry)(const PixuctHTableEntryCore *, const void *, const void *)
) {
	return pixuctHTableGet(
		(void *)pHandle,
		alloc,
		pKeyData,
		(void *)ppEntry,
		false,
		NULL,
		fpMakeKey,
		NULL,
		NULL,
		fpCompareEntry
	);
}

static inline
void pixuctHTableMemClear(PixuctHTableMem *pMem) {
	PIX_ERR_ASSERT("", pMem);
	for (I32 i = 0; i < pMem->entries.count; ++i) {
		if (pMem->entries.pArr[i].valid) {
			pixalcLinAllocClear(pMem->entries.pArr + i);
		}
	}
}

static inline
void pixuctHTableMemDestroy(const PixalcFPtrs *pAlloc, PixuctHTableMem *pMem) {
	PIX_ERR_ASSERT("", pMem);
	if (pMem->buckets.pArr) {
		pAlloc->fpFree(pMem->buckets.pArr);
	}
	for (I32 i = 0; i < pMem->entries.count; ++i) {
		if (pMem->entries.pArr[i].valid) {
			pixalcLinAllocDestroy(pMem->entries.pArr + i);
		}
	}
	*pMem = (PixuctHTableMem){0};
}

static inline
bool pixuctHTableCmpFalse(
	const PixuctHTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return false;
}

static inline
PixuctKey pixuctKeyFromI32(const void *pKeyData) {
	return (PixuctKey){.pKey = pKeyData, .size = sizeof(I32)};
}

static inline
PixuctKey pixuctKeyFromI64(const void *pKeyData) {
	return (PixuctKey){.pKey = pKeyData, .size = sizeof(I64)};
}

#define PIXUCT_AVL_MAX_DEPTH 30

typedef struct PixuctAvlNodeCore {
	U64 field;//30-left, 30-right, 1-left-valid, 1-right-valid, 2-balance
} PixuctAvlNodeCore;

static inline
PixtyValidIdx pixuctAvlChildGet(const PixuctAvlNodeCore *pNode, bool right) {
	return (PixtyValidIdx){
		.idx = (U32)(pNode->field >> (right ? 30 : 0)) & 0x3fffffffu,
		.valid = (U32)(pNode->field >> (right ? 61 : 60)) & 0x1u
	};
}

static inline
void pixuctAvlChildClear(PixuctAvlNodeCore *pNode, bool right) {
	pNode->field &= ~(
		(U64)0x3fffffffu << (right ? 30 : 0) | (U64)0x1u << (right ? 61 : 60)
	);
}

static inline
void pixuctAvlChildSet(PixuctAvlNodeCore *pNode, I32 idx, bool right) {
	PIX_ERR_ASSERT("", idx >= 0 && idx <= 0x3fffffff);
	pixuctAvlChildClear(pNode, right);
	pNode->field |= (U64)idx << (right ? 30 : 0) | (U64)0x1u << (right ? 61 : 60);
}

static inline
I32 pixuctAvlBalanceGet(const PixuctAvlNodeCore *pNode) {
	I32 raw = (I32)(pNode->field >> 62 & 0x3u);
	return raw ? raw == 1 ? 1 : -1 : 0; 
}

static inline
void pixuctAvlBalanceSet(PixuctAvlNodeCore *pNode, I32 val) {
	PIX_ERR_ASSERT("", val >= -1 && val <= 1);
	pNode->field &= ~((U64)0x3u << 62);
	pNode->field |= (U64)(val ? val == 1 ? 1 : 2 : 0) << 62;
}

typedef struct PixuctAvl {
	PixalcLinAlloc *pAlloc;
	PixuctAvlNodeCore root;
	I32 count;
} PixuctAvl;

static inline
PixErr pixuctAvlInit(PixuctAvl *pHandle, PixalcLinAlloc *pMem) {
	PixErr err = PIX_ERR_SUCCESS;
	*pHandle = (PixuctAvl){.pAlloc = pMem};
	//pixalcLinAllocInit(pAlloc, &pHandle->alloc, structSize, 6, false);
	return err;
}

static inline
bool pixuctAvlIsChildRight(PixuctAvlNodeCore *pParent, I32 node) {
	PixtyValidIdx idx = pixuctAvlChildGet(pParent, true); 
	return idx.valid && node == idx.idx;
}

static inline
void pixuctAvlRotate(
	PixuctAvl *pHandle,
	I32 node,
	PixuctAvlNodeCore *pParent,
	bool right
) {
	PixuctAvlNodeCore *pNode = pixalcLinAllocIdx(pHandle->pAlloc, node);
	bool rightOfParent = pixuctAvlIsChildRight(pParent, node);
	{
		PixtyValidIdx childLeft = pixuctAvlChildGet(pParent, false);
		PIX_ERR_ASSERT("", rightOfParent || childLeft.valid && node == childLeft.idx);
	}
	PixtyValidIdx childIdx = pixuctAvlChildGet(pNode, !right);
	PIX_ERR_ASSERT("", childIdx.valid);
	pixuctAvlChildClear(pNode, !right);
	pixuctAvlChildSet(pParent, childIdx.idx, rightOfParent);
	PixuctAvlNodeCore *pChild = pixalcLinAllocIdx(pHandle->pAlloc, childIdx.idx);
	PixtyValidIdx subChild = pixuctAvlChildGet(pChild, right);
	if (subChild.valid) {
		pixuctAvlChildClear(pChild, right);
		pixuctAvlChildSet(pNode, subChild.idx, !right);
	}
	pixuctAvlChildSet(pChild, node, right);
	pixuctAvlBalanceSet(pNode, 0);
	pixuctAvlBalanceSet(pChild, 0);
}

static inline
void pixuctAvlRotateDouble(
	PixuctAvl *pHandle,
	I32 node,
	PixuctAvlNodeCore *pParent,
	bool right
) {
	PixuctAvlNodeCore *pNode = pixalcLinAllocIdx(pHandle->pAlloc, node);
	PixtyValidIdx child = pixuctAvlChildGet(pNode, !right);
	PIX_ERR_ASSERT("", child.valid);
	PixuctAvlNodeCore *pChild = pixalcLinAllocIdx(pHandle->pAlloc, child.idx);
	PixtyValidIdx subChild = pixuctAvlChildGet(pChild, right);
	PIX_ERR_ASSERT("", subChild.valid);
	PixuctAvlNodeCore *pSubChild = pixalcLinAllocIdx(pHandle->pAlloc, subChild.idx);

	pixuctAvlChildSet(pNode, subChild.idx, !right);
	subChild = pixuctAvlChildGet(pSubChild, !right);
	if (subChild.valid) {
		pixuctAvlChildSet(pChild, subChild.idx, right);
	}
	else {
		pixuctAvlChildClear(pChild, right);
	}
	pixuctAvlChildSet(pSubChild, child.idx, !right);
	pixuctAvlBalanceSet(pChild, 0);

	pixuctAvlRotate(pHandle, node, pParent, right);
}

static inline
PixuctAvlNodeCore *pixuctAvlParentGet(
	PixuctAvl *pHandle,
	PixuctAvlNodeCore *pNode,
	I32 *pStack,
	I32 stackPtr
) {
	if (stackPtr > 0) {
		return pixalcLinAllocIdx(pHandle->pAlloc, pStack[stackPtr - 1]);
	}
	else {
		return &pHandle->root;
	}
}

PIX_FORCE_INLINE
PixErr pixuctAvlAdd(
	PixuctAvl *pHandle,
	void **ppNode,
	I32 *pIdx,
	const void *pKeyData,
	I32 (*fpCmp)(const PixuctAvlNodeCore *, const void *)
) {
	PixErr err = PIX_ERR_SUCCESS;
	PixuctAvlNodeCore *pNew = NULL;
	I32 newIdx = pixalcLinAlloc(pHandle->pAlloc, (void **)&pNew, 1);
	*pNew = (PixuctAvlNodeCore){0};
	PixuctAvlNodeCore *pNode = &pHandle->root;
	I32 stack[PIXUCT_AVL_MAX_DEPTH] = {0};
	I32 stackPtr = 0;
	bool right = true;
	do {
		PixtyValidIdx childIdx = pixuctAvlChildGet(pNode, right);
		if (!childIdx.valid) {
			pixuctAvlChildSet(pNode, newIdx, right);
			stack[stackPtr] = newIdx;
			break;
		}
		pNode = pixalcLinAllocIdx(pHandle->pAlloc, childIdx.idx);
		stack[stackPtr] = childIdx.idx;
		++stackPtr;
		PIX_ERR_ASSERT("", stackPtr < PIXUCT_AVL_MAX_DEPTH);
		I32 cmp = fpCmp(pNode, pKeyData);
		PIX_ERR_RETURN_IFNOT_COND(err, cmp != 2, "key collision");
		right = cmp;
	} while(true);
	I32 balancePrev = 0;
	//PIX_ERR_ASSERT("", stackPtr > 0);
	for (stackPtr -= 1; stackPtr >= 0; --stackPtr) {
		pNode = pixalcLinAllocIdx(pHandle->pAlloc, stack[stackPtr]);
		I32 balance = pixuctAvlBalanceGet(pNode);
		bool isRight = pixuctAvlIsChildRight(pNode, stack[stackPtr + 1]);
		balance += (isRight ? 1 : -1);
		if (!balance) {
			pixuctAvlBalanceSet(pNode, balance);
			break;
		}
		if (balance < -1 || balance > 1) {
			PixuctAvlNodeCore *pParent = pixuctAvlParentGet(pHandle, pNode, stack, stackPtr);
			if (isRight != balancePrev > 0) {
				//double rotation
				PIX_ERR_ASSERT("", balancePrev);
				pixuctAvlRotateDouble(pHandle, stack[stackPtr], pParent, !isRight);
			}
			else {
				pixuctAvlRotate(pHandle, stack[stackPtr], pParent, !isRight);
			}
			break;
		}
		pixuctAvlBalanceSet(pNode, balance);
		balancePrev = balance;
	}
	++pHandle->count;
	if (ppNode) {
		*ppNode = pNew;
	}
	if (pIdx) {
		*pIdx = newIdx;
	}
	return err;
}

typedef struct PixuctAvlStackEntry {
	I32 idx;
	I32 nextChild;
} PixuctAvlStackEntry;

typedef struct PixuctAvlIter {
	PixuctAvl *pHandle;
	PixuctAvlStackEntry stack[PIXUCT_AVL_MAX_DEPTH];
	I32 stackPtr;
	I32 linIdx;
} PixuctAvlIter;

static inline
PixErr pixuctAvlIterInit(PixuctAvl *pHandle, PixuctAvlIter *pIter) {
	PixErr err = PIX_ERR_SUCCESS;
	*pIter = (PixuctAvlIter){pHandle = pHandle};
	{
		PixtyValidIdx startIdx = pixuctAvlChildGet(&pHandle->root, true);
		PIX_ERR_RETURN_IFNOT_COND(err, startIdx.valid, "");
		pIter->stack[0].idx = startIdx.idx;
	}
	do {
		PIX_ERR_ASSERT("", pIter->stackPtr >= 0);
		PIX_ERR_RETURN_IFNOT_COND(err, pIter->stackPtr < PIXUCT_AVL_MAX_DEPTH, "");
		PixuctAvlStackEntry *pEntry = pIter->stack + pIter->stackPtr;
		PixuctAvlNodeCore *pNode = pixalcLinAllocIdx(pIter->pHandle->pAlloc, pEntry->idx);
		PixtyValidIdx child = pixuctAvlChildGet(pNode, !pEntry->nextChild);
		++pEntry->nextChild;
		if (!child.valid) {
			break;
		}
		++pIter->stackPtr;
		pIter->stack[pIter->stackPtr] = (PixuctAvlStackEntry){.idx = child.idx};
	} while(true);
	return err;
}

static inline
bool pixuctAvlIterAtEnd(const PixuctAvlIter *pIter) {
	return pIter->linIdx >= pIter->pHandle->count;
}

static inline
void pixuctAvlIterInc(PixuctAvlIter *pIter) {
	if (pixuctAvlIterAtEnd(pIter)) {
		return;
	}
	I32 start = pIter->stack[pIter->stackPtr].idx;
	do {
		PIX_ERR_ASSERT(
			"",
			pIter->stackPtr >= 0 && pIter->stackPtr < PIXUCT_AVL_MAX_DEPTH
		);
		PixuctAvlStackEntry *pEntry = pIter->stack + pIter->stackPtr;
		PixuctAvlNodeCore *pNode = pixalcLinAllocIdx(pIter->pHandle->pAlloc, pEntry->idx);
		if (pEntry->nextChild < 2) {
			PixtyValidIdx child = pixuctAvlChildGet(pNode, !pEntry->nextChild);
			++pEntry->nextChild;
			if (!child.valid) {
				if (pEntry->idx == start) {
					continue;
				}
				else {
					break;
				}
			}
			++pIter->stackPtr;
			pIter->stack[pIter->stackPtr] = (PixuctAvlStackEntry){.idx = child.idx};
		}
		else {
			if (!pIter->stackPtr) {
				break;
			}
			--pIter->stackPtr;
			PixuctAvlNodeCore *pParent = pixalcLinAllocIdx(
				pIter->pHandle->pAlloc,
				pIter->stack[pIter->stackPtr].idx
			);
			if (pixuctAvlIsChildRight(pParent, pEntry->idx)) {
				break;
			}
		}
	} while(pIter->stackPtr >= 0);
	++pIter->linIdx;
}

static inline
PixuctAvlNodeCore *pixuctAvlIterGetItem(PixuctAvlIter *pIter) {
	PIX_ERR_ASSERT(
		"",
		pIter->stackPtr >= 0 && pIter->stackPtr < PIXUCT_AVL_MAX_DEPTH
	);
	return pixalcLinAllocIdx(pIter->pHandle->pAlloc, pIter->stack[pIter->stackPtr].idx);
}

PIX_FORCE_INLINE
PixErr pixuctAvlGet(
	PixuctAvl *pHandle,
	PixuctAvlNodeCore **ppNode,
	const void *pKeyData,
	I32 (*fpCmp)(const PixuctAvlNodeCore *, const void *)
) {
	PixErr err = PIX_ERR_SUCCESS;
	PixuctAvlNodeCore *pNode = &pHandle->root;
	U32 rootValid = (U32)(pNode->field >> 61) & 0x1u;
	if (!rootValid) {
		//tree is empty
		*ppNode = NULL;
		return err;
	}
	//get first node (stored in root right child)
	PIX_ERR_ASSERT("", rootValid == 0x1u);
	pNode = pixalcLinAllocIdx(pHandle->pAlloc, pNode->field >> 30 & 0x3fffffffu);
	I32 depth = 0;
	while (true) {
		bool right;
		{
			I32 cmp = fpCmp(pNode, pKeyData);
			if (cmp == 2) {
				*ppNode = pNode;
				return err;
			}
			right = cmp;
		}
		PixtyValidIdx childIdx = pixuctAvlChildGet(pNode, right);
		if (!childIdx.valid) {
			//key isn't in tree
			*ppNode = NULL;
			return err;
		}
		pNode = pixalcLinAllocIdx(pHandle->pAlloc, childIdx.idx);
		++depth;
		PIX_ERR_ASSERT("", depth < PIXUCT_AVL_MAX_DEPTH);
	}
	return err;
}

static inline
void pixuctAvlClear(PixuctAvl *pHandle) {
	PIX_ERR_ASSERT(
		"",
		pHandle->count &&
		pHandle->root.field &&
		pHandle->pAlloc && pHandle->pAlloc->valid
	);
	*pHandle = (PixuctAvl){.pAlloc = pHandle->pAlloc};
}
