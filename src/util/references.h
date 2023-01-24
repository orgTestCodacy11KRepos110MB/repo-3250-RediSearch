/*
 * Copyright Redis Ltd. 2016 - present
 * Licensed under your choice of the Redis Source Available License 2.0 (RSALv2) or
 * the Server Side Public License v1 (SSPLv1).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StrongRef {
  struct IndexSpecManager *ism;
} StrongRef;

typedef struct WeakRef {
  struct IndexSpecManager *ism;
} WeakRef;

WeakRef WeakRef_Clone(WeakRef ref);
StrongRef WeakRef_Promote(WeakRef w_ref);
void WeakRef_Release(WeakRef w_ref);

StrongRef StrongRef_Clone(StrongRef ref);
WeakRef StrongRef_Demote(StrongRef s_ref);
void StrongRef_Release(StrongRef s_ref);
struct IndexSpec *StrongRef_Get(StrongRef s_ref);

StrongRef StrongRef_New(struct IndexSpec *sp);

static inline int StrongRef_Equals(StrongRef s_ref, StrongRef other) {
  return s_ref.ism == other.ism;
}

#ifdef __cplusplus
}
#endif
