#include "rlookup.h"
#include "module.h"
#include "document.h"
#include "util/arr.h"

#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////

RLookupKey::RLookupKey(RLookup *lookup, const char *name_, size_t n, int flags_, uint16_t idx)
  : dstidx{idx}
  , flags{static_cast<uint16_t>(flags_ & (~RLOOKUP_TRANSIENT_FLAGS))}
  , refcnt{1}
  , name{flags_ & RLOOKUP_F_NAMEALLOC ? rm_strndup(name_, n) : name_}
{
  if (!lookup->head) {
    lookup->head = this;
  } else {
    lookup->tail->next = this;
  }
  lookup->tail = this;
}

///////////////////////////////////////////////////////////////////////////////////////////////

RLookupKey *RLookup::genKeyFromSpec(const char *name, int flags) {
  if (!spcache) {
    return nullptr;
  }

  const FieldSpec *fs = nullptr;
  for (auto const &field : *spcache) {
    if (field.name == name) {
      fs = &field;
      break;
    }
  }

  if (!fs) {
    // Field does not exist in the schema at all
    return nullptr;
  }

  uint16_t idx = rowlen++;

  auto ret = new RLookupKey(this, name, strlen(name), flags, idx);
  if (fs->IsSortable()) {
    ret->flags |= RLOOKUP_F_SVSRC;
    ret->svidx = fs->sortIdx;
  }
  ret->flags |= RLOOKUP_F_DOCSRC;
  if (fs->types == INDEXFLD_T_NUMERIC) {
    ret->fieldtype = RLOOKUP_C_DBL;
  }
  return ret;
}

//---------------------------------------------------------------------------------------------

RLookupKey *RLookup::GetKey(const char *name, size_t n, int flags) {
  RLookupKey *ret = nullptr;

  for (RLookupKey *kk = head; kk; kk = kk->next) {
    size_t origlen = strlen(kk->name);
    if (origlen == n && !strncmp(kk->name, name, origlen)) {
      if (flags & RLOOKUP_F_OEXCL) {
        return nullptr;
      }
      ret = kk;
      break;
    }
  }

  if (!ret) {
    ret = genKeyFromSpec(name, flags);
  }

  if (!ret) {
    if (!(flags & RLOOKUP_F_OCREAT) && !(options & RLOOKUP_OPT_UNRESOLVED_OK)) {
      return nullptr;
    } else {
      ret = new RLookupKey(this, name, n, flags, rowlen++);
      if (!(flags & RLOOKUP_F_OCREAT)) {
        ret->flags |= RLOOKUP_F_UNRESOLVED;
      }
    }
  }

  if (!(flags & RLOOKUP_F_NOINCREF)) {
    ret->refcnt++;
  }

  if (flags & RLOOKUP_F_OCREAT) {
    // If the requester of this key is also its creator, remove the unresolved flag
    ret->flags &= ~RLOOKUP_F_UNRESOLVED;
  }
  return ret;
}

//---------------------------------------------------------------------------------------------

/**
 * Get a RLookup key for a given name. The behavior of this function depends on
 * the flags.
 *
 * If F_OCREAT is not used, then this function will return nullptr if a key could
 * not be found, unless OPT_UNRESOLVED_OK is set on the lookup itself. In this
 * case, the key is returned, but has the F_UNRESOLVED flag set.
 */

RLookupKey *RLookup::GetKey(const char *name, int flags) {
  return GetKey(name, strlen(name), flags);
}

RLookupKey *RLookup::GetKey(std::string_view name, int flags) {
  return GetKey(name.data(), name.length(), flags);
}

//---------------------------------------------------------------------------------------------

size_t RLookup::GetLength(const RLookupRow *r, int requiredFlags, int excludeFlags) const {
  size_t nfields = 0;
  for (const RLookupKey *kk = head; kk; kk = kk->next) {
    if (requiredFlags && !(kk->flags & requiredFlags)) {
      continue;
    }
    if (excludeFlags && (kk->flags & excludeFlags)) {
      continue;
    }
    const RSValue *v = r->GetItem(kk);
    if (!v) {
      continue;
    }

    ++nfields;
  }
  return nfields;
}

//---------------------------------------------------------------------------------------------

/**
 * Initialize the lookup. If cache is provided, then it will be used as an
 * alternate source for lookups whose fields are absent
 */

RLookup::RLookup(std::shared_ptr<IndexSpecFields> spcache) : head(nullptr), tail(nullptr), rowlen(0), options(0),
  spcache(spcache) {
}

//---------------------------------------------------------------------------------------------

void RLookup::Reset(std::shared_ptr<IndexSpecFields> cache) {
  head = nullptr;
  tail = nullptr;
  rowlen = 0;
  options = 0;
  spcache = cache;
}

//---------------------------------------------------------------------------------------------

/**
 * Exactly like WriteKey, but does not increment the refcount, allowing
 * idioms such as WriteKey(..., RS_NumVal(10)); which would otherwise cause
 * a leak.
 */

void RLookupRow::WriteOwnKey(const RLookupKey *key, RSValue *v) {
  // Find the pointer to write to ...
  if (dyn.size() <= key->dstidx) {
    dyn.resize(key->dstidx + 1);
  }
  RSValue *vptr = dyn[key->dstidx];
  if (vptr) {
    vptr->Decref();
  }
  dyn[key->dstidx] = v;
}

//---------------------------------------------------------------------------------------------

/**
 * Write a value to a lookup table. Key must already be registered, and not
 * refer to a read-only (SVSRC) key.
 *
 * The value written will have its refcount incremented
 */

void RLookupRow::WriteKey(const RLookupKey *key, RSValue *v) {
  WriteOwnKey(key, v);
  v->IncrRef();
}

//---------------------------------------------------------------------------------------------

/**
 * Write a value by-name to the lookup table. This is useful for 'dynamic' keys
 * for which it is not necessary to use the boilerplate of getting an explicit
 * key.
 *
 * The reference count of the value will be incremented.
 */

void RLookup::WriteKeyByName(const char *name, RLookupRow *dst, RSValue *v) {
  // Get the key first
  RLookupKey *k = GetKey(name, RLOOKUP_F_NAMEALLOC | RLOOKUP_F_NOINCREF | RLOOKUP_F_OCREAT);
  if (!k) throw Error("failed to get key");
  dst->WriteKey(k, v);
}

//---------------------------------------------------------------------------------------------

void RLookup::WriteOwnKeyByName(const char *name, RLookupRow *row, RSValue *value) {
  WriteKeyByName(name, row, value);
  value->Decref();
}

//---------------------------------------------------------------------------------------------

/**
 * Wipes the row, retaining its memory but decrefing any included values.
 * This does not free all the memory consumed by the row, but simply resets
 * the row data (preserving any caches) so that it may be refilled.
 */

void RLookupRow::Wipe() {
  for (auto vpp : dyn) {
    if (vpp) {
      vpp->Decref();
    }
  }
  dyn.clear();
  sv = nullptr;
  if (rmkey) {
    RedisModule_CloseKey(rmkey);
    rmkey = nullptr;
  }
}

//---------------------------------------------------------------------------------------------

/** Get a value from the row, provided the key.
 *
 * This does not actually "search" for the key, but simply performs array lookups!
 *
 * @param lookup The lookup table containing the lookup table data
 * @param key the key that contains the index
 * @param row the row data which contains the value
 * @return the value if found, nullptr otherwise.
 */

RSValue *RLookupRow::GetItem(const RLookupKey *key) const {
  RSValue *ret = nullptr;
  if (dyn.size() > key->dstidx) {
    ret = dyn[key->dstidx];
  }
  if (!ret) {
    if (key->flags & RLOOKUP_F_SVSRC) {
      if (sv && sv->len > key->svidx) {
        ret = sv->values[key->svidx];
        if (ret != nullptr && ret->t == RSValue_Null) {
          ret = nullptr;
        }
      }
    }
  }
  return ret;
}

//---------------------------------------------------------------------------------------------

// Frees all the memory consumed by the row. Implies Wipe().
// This should be used when the row object will no longer be used.

void RLookupRow::Cleanup() {
  Wipe();
}

//---------------------------------------------------------------------------------------------

/**
 * Move data from the source row to the destination row. The source row is cleared.
 * The destination row should be pre-cleared (though its cache may still
 * exist).
 * @param lk lookup common to both rows
 * @param src the source row
 * @param dst the destination row
 */

void RLookup::MoveRow(RLookupRow *src, RLookupRow *dst) const {
  for (const RLookupKey *kk = head; kk; kk = kk->next) {
    RSValue *vv = src->GetItem(kk);
    if (vv) {
      dst->WriteKey(kk, vv);
    }
  }
  src->Wipe();
}

//---------------------------------------------------------------------------------------------

void RLookupRow::Dump() const {
  printf("Row @%p\n", this);
  if (!dyn.empty()) {
    printf("  DYN @%p\n", dyn.data());
    for (size_t ii = 0; ii < dyn.size(); ++ii) {
      printf("  [%lu]: %p\n", ii, dyn[ii]);
      if (dyn[ii]) {
        printf("    ");
        dyn[ii]->Print();
        printf("\n");
      }
    }
  }
  if (sv) {
    printf("  SV @%p\n", sv);
  }
}

//---------------------------------------------------------------------------------------------

RLookupKey::~RLookupKey() {
  if (flags & RLOOKUP_F_NAMEALLOC) {
    rm_free((void *)name);
  }
}

//---------------------------------------------------------------------------------------------

/**
 * Releases any resources created by this lookup object. Note that if there are
 * lookup keys created with RLOOKUP_F_NOINCREF, those keys will no longer be
 * valid after this call!
 */

RLookup::~RLookup() {
  RLookupKey *next, *cur = head;
  while (cur) {
    next = cur->next;
    delete cur;
    cur = next;
  }
  if (spcache) {
    // delete spcache;
    spcache.reset();
  }
}

//---------------------------------------------------------------------------------------------

static RSValue *hvalToValue(RedisModuleString *src, RLookupCoerceType type) {
  if (type == RLOOKUP_C_BOOL || type == RLOOKUP_C_INT) {
    long long ll = 0;
    RedisModule_StringToLongLong(src, &ll);
    return RS_Int64Val(ll);
  } else if (type == RLOOKUP_C_DBL) {
    double dd = 0.0;
    RedisModule_StringToDouble(src, &dd);
    return RS_NumVal(dd);
  } else {
    return RS_OwnRedisStringVal(src);
  }
}

//---------------------------------------------------------------------------------------------

static RSValue *replyElemToValue(RedisModuleCallReply *rep, RLookupCoerceType otype) {
  switch (RedisModule_CallReplyType(rep)) {
    case REDISMODULE_REPLY_STRING: {
      if (otype == RLOOKUP_C_BOOL || RLOOKUP_C_INT) {
        goto create_int;
      }

    create_string:;
      size_t len;
      const char *s = RedisModule_CallReplyStringPtr(rep, &len);
      if (otype == RLOOKUP_C_DBL) {
        // Convert to double -- calling code should check if nullptr
        return RSValue_ParseNumber(s, len);
      }
      // Note, the pointer is within CallReply; we need to copy
      return RS_NewCopiedString(s, len);
    }

    case REDISMODULE_REPLY_INTEGER:
    create_int:
      if (otype == RLOOKUP_C_STR || otype == RLOOKUP_C_DBL) {
        goto create_string;
      }
      return RS_Int64Val(RedisModule_CallReplyInteger(rep));

    case REDISMODULE_REPLY_UNKNOWN:
    case REDISMODULE_REPLY_NULL:
    case REDISMODULE_REPLY_ARRAY:
    default:
      // Nothing
      return RS_NullVal();
  }
}

//---------------------------------------------------------------------------------------------

int RLookupRow::getKeyCommon(const RLookupKey *kk, RLookupLoadOptions *options,
                             RedisModuleKey **keyobj) {
  if (!options->noSortables && (kk->flags & RLOOKUP_F_SVSRC)) {
    // No need to "write" this key. It's always implicitly loaded!
    return REDISMODULE_OK;
  }

  // In this case, the flag must be obtained via HGET
  if (!*keyobj) {
    RedisModuleCtx *ctx = options->sctx->redisCtx;
    RedisModuleString *keyName =
        RedisModule_CreateString(ctx, options->dmd->keyPtr, strlen(options->dmd->keyPtr));
    *keyobj = static_cast<RedisModuleKey *>(RedisModule_OpenKey(ctx, keyName, REDISMODULE_READ));
    RedisModule_FreeString(ctx, keyName);
    if (!*keyobj) {
      options->status->SetCode(QUERY_ENODOC);
      return REDISMODULE_ERR;
    }
    if (RedisModule_KeyType(*keyobj) != REDISMODULE_KEYTYPE_HASH) {
      options->status->SetCode(QUERY_EREDISKEYTYPE);
      return REDISMODULE_ERR;
    }
  }

  // Get the actual hash value
  RedisModuleString *val = nullptr;
  int rc = RedisModule_HashGet(*keyobj, REDISMODULE_HASH_CFIELDS, kk->name, &val, nullptr);
  if (rc != REDISMODULE_OK || val == nullptr) {
    return REDISMODULE_OK;
  }

  // Value has a reference count of 1
  RSValue *rsv = hvalToValue(val, kk->fieldtype);
  RedisModule_FreeString(RSDummyContext, val);
  WriteKey(kk, rsv);
  rsv->Decref();
  return REDISMODULE_OK;
}

//---------------------------------------------------------------------------------------------

int RLookup::loadIndividualKeys(RLookupRow *dst, RLookupLoadOptions *options) {
  // Load the document from the schema. This should be simple enough...
  RedisModuleKey *key = nullptr;  // This is populated by getKeyCommon; we free it at the end
  int rc = REDISMODULE_ERR;
  if (options->nkeys) {
    for (size_t ii = 0; ii < options->nkeys; ++ii) {
      const RLookupKey *kk = options->keys[ii];
      if (dst->getKeyCommon(kk, options, &key) != REDISMODULE_OK) {
        goto done;
      }
    }
  } else {
    for (const RLookupKey *kk = head; kk; kk = kk->next) {
      // key is not part of document schema. no need/impossible to 'load' it
      if (!(kk->flags & RLOOKUP_F_DOCSRC)) {
        continue;
      }
      if (!options->noSortables) {
        // wanted a sort key, but field is not sortable
        if ((options->mode & RLOOKUP_LOAD_SVKEYS) && !(kk->flags & RLOOKUP_F_SVSRC)) {
          continue;
        }
      }
      if (dst->getKeyCommon(kk, options, &key) != REDISMODULE_OK) {
        goto done;
      }
    }
  }
  rc = REDISMODULE_OK;

done:
  if (key) {
    RedisModule_CloseKey(key);
  }
  return rc;
}

//---------------------------------------------------------------------------------------------

int RLookup::HGETALL(RLookupRow *dst, RLookupLoadOptions *options) {
  int rc = REDISMODULE_ERR;
  size_t len;
  RedisModuleCallReply *rep = nullptr;
  RedisModuleCtx *ctx = options->sctx->redisCtx;
  RedisModuleString *krstr =
    RedisModule_CreateString(ctx, options->dmd->keyPtr, sdslen(options->dmd->keyPtr));

  rep = RedisModule_Call(ctx, "HGETALL", "s", krstr);
  if (rep == nullptr || RedisModule_CallReplyType(rep) != REDISMODULE_REPLY_ARRAY) {
    goto done;
  }

  len = RedisModule_CallReplyLength(rep);
  // Zero means the document does not exist in redis
  if (len == 0) {
    goto done;
  }

  for (size_t i = 0; i < len; i += 2) {
    size_t klen = 0;
    RedisModuleCallReply *repk = RedisModule_CallReplyArrayElement(rep, i);
    RedisModuleCallReply *repv = RedisModule_CallReplyArrayElement(rep, i + 1);

    const char *kstr = RedisModule_CallReplyStringPtr(repk, &klen);
    RLookupKey *rlk = GetKey(kstr, klen, RLOOKUP_F_OCREAT | RLOOKUP_F_NAMEALLOC);
    if (!options->noSortables && (rlk->flags & RLOOKUP_F_SVSRC)) {
      continue;  // Can load it from the sort vector on demand.
    }
    RLookupCoerceType ctype = rlk->fieldtype;
    if (options->forceString) {
      ctype = RLOOKUP_C_STR;
    }
    RSValue *vptr = replyElemToValue(repv, ctype);
    dst->WriteOwnKey(rlk, vptr);
  }

  rc = REDISMODULE_OK;

done:
  if (krstr) {
    RedisModule_FreeString(ctx, krstr);
  }
  if (rep) {
    RedisModule_FreeCallReply(rep);
  }
  return rc;
}

//---------------------------------------------------------------------------------------------

/**
 * Attempt to load a document into the row. The document's fields are placed into
 * their corresponding slots.
 *
 * @param lt Lookup table. Contains the keys to load.
 * @param dst row that should contain the data
 * @param options options controlling the load process
 */

int RLookup::LoadDocument(RLookupRow *dst, RLookupLoadOptions *options) {
  if (options->dmd) {
    dst->sv = options->dmd->sortVector;
  }
  if (options->mode & RLOOKUP_LOAD_ALLKEYS) {
    return HGETALL(dst, options);
  } else {
    return loadIndividualKeys(dst, options);
  }
}

//---------------------------------------------------------------------------------------------

const RLookupKey *RLookup::FindKeyWith(uint32_t f) const {
  for (const RLookupKey *k = head; k; k = k->next) {
    if (k->flags & f) {
      return k;
    }
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////
