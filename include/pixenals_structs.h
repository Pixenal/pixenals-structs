/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <string.h>

#include "../../pixenals-alloc-utils/include/pixenals_alloc_utils.h"
#include "../../pixenals-error-utils/include/pixenals_error_utils.h"

#ifndef PIX_FORCE_INLINE
#ifdef WIN32
#define PIX_FORCE_INLINE __forceinline
#else
#define PIX_FORCE_INLINE __attribute__((always_inline)) static inline
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

typedef struct PixuctHTable {
	const PixalcFPtrs *pAlloc;
	PixalcLinAlloc allocHandles[PIX_HTABLE_ALLOC_HANDLES_MAX];
	PixalcLinAlloc *linAlc[PIX_HTABLE_ALLOC_HANDLES_MAX];
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

typedef struct PixuctHTableMem {
	PixalcLinAlloc pArr[PIX_HTABLE_ALLOC_HANDLES_MAX];
	I32 count;
} PixuctHTableMem;

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

static inline
PixalcLinAlloc *pixuctHTableAllocGet(PixuctHTable *pHandle, I32 idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < PIX_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->linAlc[idx];
}

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
	PIX_ERR_ASSERT(
		"",
		alloc < PIX_HTABLE_ALLOC_HANDLES_MAX &&
		pHandle->linAlc[alloc] && pHandle->linAlc[alloc]->valid
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
		I32 linIdx =
			pixalcLinAlloc(pHandle->linAlc[alloc], (void **)&pBucket->pList, 1);
		fpInitEntry(pHandle->pUserData, pBucket->pList, pKeyData, pInitInfo, linIdx);
		if (ppEntry) {
			*ppEntry = pBucket->pList;
		}
		return PIX_SEARCH_ADDED;
	}
	PixuctHTableEntryCore *pEntry = pBucket->pList;
	do {
		if (fpCompareEntry(pEntry, pKeyData, pInitInfo)) {
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
			I32 linIdx =
				pixalcLinAlloc(pHandle->linAlc[alloc], (void **)&pEntry->pNext, 1);
			fpInitEntry(pHandle->pUserData, pEntry->pNext, pKeyData, pInitInfo, linIdx);
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
	PIX_ERR_ASSERT(
		"",
		alloc < PIX_HTABLE_ALLOC_HANDLES_MAX && 
		pHandle->linAlc[alloc] && pHandle->linAlc[alloc]->valid
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
	pixalcLinAllocRegionClear(pHandle->linAlc[alloc], pEntry, 1);
}

PIX_FORCE_INLINE
SearchResult pixuctHTableGetConst(
	PixuctHTable *pHandle,
	I32 alloc,
	const void *pKeyData,
	void **ppEntry,
	bool addEntry,
	const void *pInitInfo,
	PixuctKey (* fpMakeKey)(const void *),
	bool (* fpAddPredicate)(const void *, const void *, const void *),
	void (* fpInitEntry)(void *, PixuctHTableEntryCore *, const void *, void *, I32),
	bool (* fpCompareEntry)(const PixuctHTableEntryCore *, const void *, const void *)
) {
	return pixuctHTableGet(
		pHandle,
		alloc,
		pKeyData,
		ppEntry,
		addEntry,
		(void *)pInitInfo,
		fpMakeKey,
		fpAddPredicate,
		fpInitEntry,
		fpCompareEntry
	);
}

static inline
void pixuctHTableMemDestroy(PixuctHTableMem *pMem) {
	PIX_ERR_ASSERT("", pMem && pMem->count);
	for (I32 i = 0; i < pMem->count; ++i) {
		if (pMem->pArr[i].valid) {
			pixalcLinAllocDestroy(pMem->pArr + i);
		}
	}
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
void pixuctAvlChildSet(PixuctAvlNodeCore *pNode, I32 idx, bool right) {
	PIX_ERR_ASSERT("", idx >= 0 && idx <= 0x3fffffff);
	pNode->field |= (U64)idx << (right ? 30 : 0) | (U64)0x1u << (right ? 61 : 60);
}

static inline
void pixuctAvlChildClear(PixuctAvlNodeCore *pNode, bool right) {
	pNode->field &= ~((U64)0x1u << (right ? 61 : 60));
}

static inline
I32 pixuctAvlBalanceGet(const PixuctAvlNodeCore *pNode) {
	return ((I32)(pNode->field >> 62) & 0x3) - 1;
}

static inline
void pixuctAvlBalanceSet(PixuctAvlNodeCore *pNode, I32 val) {
	PIX_ERR_ASSERT("", val >= -1 && val <= 1);
	pNode->field |= (U64)(val + 1) << 62;
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

//TODO use custom bitfield in node struct rather than builtin.
// make get/set funcs to reduce repeated code
static inline
void pixuctAvlRotate(
	PixuctAvl *pHandle,
	I32 node,
	PixuctAvlNodeCore *pParent,
	bool right
) {
	PixuctAvlNodeCore *pNode = pixalcLinAllocIdx(pHandle->pAlloc, node);
	bool rightOfParent = node == pixuctAvlChildGet(pParent, true).idx;
	PIX_ERR_ASSERT("", rightOfParent || node == pixuctAvlChildGet(pParent, false).idx);
	PixtyValidIdx childIdx = pixuctAvlChildGet(pNode, !right);
	PIX_ERR_ASSERT("", childIdx.valid);
	pixuctAvlChildSet(pParent, childIdx.idx, rightOfParent);
	PixuctAvlNodeCore *pChild = pixalcLinAllocIdx(pHandle->pAlloc, childIdx.idx);
	PixtyValidIdx subChild = pixuctAvlChildGet(pChild, right);
	if (subChild.valid) {
	}
	else {
		pixuctAvlChildClear(pNode, !right);
		pixuctAvlChildSet(pNode, subChild.idx, !right);
		I32 balance = pixuctAvlBalanceGet(pNode);
		pixuctAvlBalanceSet(pNode, balance - (right ? 1 : -1));
	}
	pixuctAvlChildSet(pChild, node, right);
}

static inline
PixuctAvlNodeCore *pixuctAvlParentGet(
	PixuctAvl *pHandle,
	PixuctAvlNodeCore *pNode,
	I32 *pStack,
	I32 stackPtr
) {
	PixErr err = PIX_ERR_SUCCESS;
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
	bool (*fpCmp)(const PixuctAvlNodeCore *, const void *)
) {
	PixErr err = PIX_ERR_SUCCESS;
	PixuctAvlNodeCore *pNew = NULL;
	I32 newIdx = pixalcLinAlloc(pHandle->pAlloc, &pNew, 1);
	PixuctAvlNodeCore *pNode = &pHandle->root;
	I32 stack[PIXUCT_AVL_MAX_DEPTH] = {0};
	I32 stackPtr = 0;
	while (true) {
		bool right = fpCmp(pNode, pKeyData);
		PixtyValidIdx childIdx = pixuctAvlChildGet(pNode, right);
		if (!childIdx.valid) {
			pixuctAvlChildSet(pNode, newIdx, right);
			break;
		}
		pNode = pixalcLinAllocIdx(pHandle->pAlloc, childIdx.idx);
		stack[stackPtr] = childIdx.idx;
		++stackPtr;
		PIX_ERR_ASSERT("", stackPtr < PIXUCT_AVL_MAX_DEPTH);
	}
	I32 balancePrev = 0;
	PIX_ERR_ASSERT("", stackPtr > 0);
	for (stackPtr -= 1; stackPtr >= 0; --stackPtr) {
		pNode = pixalcLinAllocIdx(pHandle->pAlloc, stack[stackPtr]);
		I32 balance = pixuctAvlBalanceGet(pNode);
		if (!balance) {
			break;
		}
		PixuctAvlNodeCore *pParent = pixuctAvlParentGet(pHandle, pNode, stack, stackPtr);
		bool isRight = stack[stackPtr] == pixuctAvlChildGet(pParent, true).idx;
		balance += (isRight ? 1 : -1);
		if (balance < -1 || balance > 1) {
			PIX_ERR_ASSERT("", isRight == balance > 0);
			pixuctAvlRotate(pHandle, stack[stackPtr], pParent, !isRight);
			if (isRight != balancePrev > 0) {
				//double rotation
				PIX_ERR_ASSERT("", balancePrev);
				pixuctAvlRotate(pHandle, stack[stackPtr], pParent, isRight);
			}
			pixuctAvlBalanceSet(pParent, 0);
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
	return err;
}

static inline
bool pixuctAvlIterAtEnd(const PixuctAvlIter *pIter) {
	return pIter->linIdx < pIter->pHandle->count;
}

static inline
void pixuctAvlIterInc(PixuctAvlIter *pIter) {
	if (pixuctAvlIterAtEnd(pIter)) {
		return;
	}
	do {
		PIX_ERR_ASSERT(
			"",
			pIter->stackPtr >= 0 && pIter->stackPtr < PIXUCT_AVL_MAX_DEPTH
		);
		PixuctAvlStackEntry *pEntry = pIter->stack + pIter->stackPtr;
		PixuctAvlNodeCore *pNode = pixalcLinAllocIdx(pIter->pHandle->pAlloc, pEntry->idx);
		if (pEntry->nextChild < 2) {
			PixtyValidIdx child = pixuctAvlChildGet(pNode, pEntry->nextChild);
			++pEntry->nextChild;
			if (!child.valid) {
				continue;
			}
			++pIter->stackPtr;
			pIter->stack[pIter->stackPtr] = (PixuctAvlStackEntry){.idx = child.idx};
			break;
		}
		else {
			--pIter->stackPtr;
		}
	} while(++pIter->linIdx, !pixuctAvlIterAtEnd(pIter));
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
	bool (*fpCmp)(const PixuctAvlNodeCore *, const void *),
	bool (*fpCmpEql)(const PixuctAvlNodeCore *, const void *)
) {
	PixErr err = PIX_ERR_SUCCESS;
	PixuctAvlNodeCore *pNode = &pHandle->root;
	U32 rootValid = (U32)(pNode->field >> 60) & 0x3u;
	if (!rootValid) {
		//tree is empty
		*ppNode = NULL;
		return err;
	}
	//get first node (stored in root left child)
	PIX_ERR_ASSERT("", rootValid == 0x1u);
	pNode = pixalcLinAllocIdx(pHandle->pAlloc, pNode->field & 0x3fffffffu);
	I32 depth = 0;
	while (true) {
		if (fpCmpEql(pNode, pKeyData)) {
			*ppNode = pNode;
			return err;
		}
		bool right = fpCmp(pNode, pKeyData);
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
