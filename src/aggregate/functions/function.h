
#pragma once

#include "value.h"
#include "util/block_alloc.h"
#include "result_processor.h"
#include "query_error.h"

///////////////////////////////////////////////////////////////////////////////////////////////

#define VALIDATE_ARGS(fname, minargs, maxargs, err)                                           \
  if (argc < minargs || argc > maxargs) {                                                     \
    err->SetError(QUERY_EPARSEARGS, "Invalid arguments for function '" fname "'");            \
    return EXPR_EVAL_ERR;                                                                     \
  }

#define VALIDATE_ARG__COMMON(fname, args, idx, verifier, varg)                                 \
  {                                                                                            \
    RSValue *dref = args[idx]->Dereference();                                                  \
    if (!verifier(dref, varg)) {                                                               \
      err->SetErrorFmt(QUERY_EPARSEARGS,                                                       \
          "Invalid type (%d) for argument %d in function '%s'. %s(v, %s) was false.", dref->t, \
          idx, fname, #verifier, #varg);                                                       \
      return EXPR_EVAL_ERR;                                                                    \
    }                                                                                          \
  }

#define VALIDATE_ARG__TYPE(arg, t_) ((arg)->t == t_)
#define VALIDATE_ARG_TYPE(fname, args, idx, t) \
  VALIDATE_ARG__COMMON(fname, args, idx, VALIDATE_ARG__TYPE, t)

#define VALIDATE_ARG__STRING(arg, unused) (arg)->IsString()
#define VALIDATE_ARG_ISSTRING(fname, args, idx) \
  VALIDATE_ARG__COMMON(fname, args, idx, VALIDATE_ARG__STRING, 0)

//---------------------------------------------------------------------------------------------

struct ExprEval;

/**
 * Function callback for arguments.
 * @param e Evaluator context. Can be used for allocations and other goodies
 * @param[out] result Store the result of the function here. Can be a reference
 * @param args The arguments passed to the function. This can be:
 *  NULL (no arguments)
 *  String value (raw)
 *  Converted value (numeric, reference, etc.)
 * @nargs the number of arguments passed
 * @err If an error occurs, return EXPR_EVAL_ERR with the error set here.
 *
 * @return EXPR_EVAL_ERR or EXPR_EVAL_OK
 */
typedef int (*RSFunction)(ExprEval *e, RSValue *result, RSValue **args, size_t nargs,
                          QueryError *err);

//---------------------------------------------------------------------------------------------

struct RSFunctionRegistry {
  size_t len;
  size_t cap;
  struct RSFunctionInfo {
    RSFunction f;
    const char *name;
    RSValueType retType;
    unsigned minargs;
    int maxargs;
  } * funcs;
};

//---------------------------------------------------------------------------------------------

typedef struct RSFunctionInfo RSFunctionInfo;

RSFunction RSFunctionRegistry_Get(const char *name, size_t len);

int RSFunctionRegistry_RegisterFunction(const char *name, RSFunction f, RSValueType retType);

void RegisterMathFunctions();
void RegisterStringFunctions();
void RegisterDateFunctions();
void RegisterAllFunctions();

void FunctionRegistry_Free();

///////////////////////////////////////////////////////////////////////////////////////////////
