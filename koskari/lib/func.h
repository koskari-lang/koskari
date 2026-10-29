/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KOSKARI_FUNC_H
#define KOSKARI_FUNC_H


#include <Python.h>
#include "callable.h"
#include "namespace.h"


/* === Expression === */


extern KosEvaluatedTypeObject KosExpressionType;


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *ns;
} KosExpression;


/** Borrowed reference. Do not Py_DECREF the result. */
#define KosExpression_CODE(expr) (((KosExpression *) (expr))->code)

/** Borrowed reference. Do not Py_DECREF the result. */
#define KosExpression_NS(expr)   (((KosExpression *) (expr))->ns)

#define KosExpression_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosExpressionType)))


/**
   Create an Expression (code, ns). Used by Code.expression(parent_ns).

   Calling the returned Expression with () runs the code in the namespace
   and returns a Coroutine.

   :param code: KosCode instance
   :param ns: KosNamespace instance (parent/global namespace)
   :return: new Expression, or NULL on error
*/
PyObject *KosExpression_New(PyObject *code, PyObject *ns);


/**
   Run the expression's code in its namespace and return a new Coroutine.

   :param expr: KosExpression instance
   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosExpression_Eval(PyObject *expr);


/**
   Return the expression's code (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosExpression_GetCode(PyObject *expr);

/**
   Return the expression's namespace (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosExpression_GetNs(PyObject *expr);


/* === FunctionSpec === */


extern PyTypeObject KosFunctionSpecType;


typedef struct KosFunctionSpec {
  PyObject_HEAD

  PyObject *name;           /* PyUnicode or NULL */
  PyObject *formals_spec;   /* KosFormalsSpec */
  PyObject *code;           /* KosCode */
  int self_slot;            /* fast slot of name among the code's locals,
                               or -1 when name is NULL or not a local.
                               Only the named-lambda form declares the
                               function's own name as a body local;
                               define and function refer to the outer
                               binding. Computed once here, since every
                               function built from this spec shares it */
} KosFunctionSpec;


PyObject *KosFunctionSpec_New(
      PyObject *name,
      PyObject *formals_spec,
      PyObject *code);


#define KosFunctionSpec_NAME(fspec)             \
  (((KosFunctionSpec *) (fspec))->name)

#define KosFunctionSpec_FORMALS_SPEC(fspec)    \
  (((KosFunctionSpec *) (fspec))->formals_spec)

#define KosFunctionSpec_CODE(fspec)            \
  (((KosFunctionSpec *) (fspec))->code)

#define KosFunctionSpec_SELF_SLOT(fspec)       \
  (((KosFunctionSpec *) (fspec))->self_slot)

#define KosFunctionSpec_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosFunctionSpecType))


/**
   Create a Function from a FunctionSpec and keyword defaults.
   Used by BUILD_FUNCTION: formals = KosFormals_New(fspec->formals_spec,
   kw_defaults), then KosFunction_NewFromFormals(fspec->code, formals,
   parent_ns).

   :param func_spec: KosFunctionSpec instance
   :param kw_defaults: PyDict of name -> default value, or NULL
   :param parent_namespace: KosNamespace (current frame NS)
   :return: new KosFunction, or NULL on error
*/
PyObject *KosFunction_New(
      PyObject *func_spec,
      PyObject *kw_defaults,
      PyObject *parent_namespace);


/**
   Create a Function and attach a creation-time annotations dict in one
   call. Used by BUILD_FUNCTION when its annotation names tuple is
   non-empty; also the entry point for non-opcode callers building
   annotated functions.

   The annotations dict may mix real objects with str forward
   references; str values are resolved lazily against the defining
   namespace on first __annotations__ / __signature__ access.

   :param func_spec: KosFunctionSpec instance
   :param kw_defaults: PyDict of name -> default value, or NULL
   :param parent_namespace: KosNamespace (current frame NS)
   :param annotations: PyDict of name -> annotation value, or NULL
   :return: new KosFunction, or NULL on error
*/
PyObject *KosFunction_NewWithAnnotations(
      PyObject *func_spec,
      PyObject *kw_defaults,
      PyObject *parent_namespace,
      PyObject *annotations);


/* === Function === */


extern KosEvaluatedTypeObject KosFunctionType;


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *base_ns;   /* template namespace; parent chain via template->parent */
  PyObject *formals;
  PyObject *name;           /* proper name for self-reference and repr, or NULL */
  vectorcallfunc vectorcall; /* for Py_TPFLAGS_HAVE_VECTORCALL; always set */
  PyObject *inspect_signature; /* lazily built inspect.Signature, or NULL */
  PyObject *doc;            /* __doc__ from Code.extras["doc"] if unicode; NULL */
  PyObject *dict;           /* instance __dict__ for arbitrary attributes; NULL */
  PyObject *creation_annotations; /* raw dict from BUILD_FUNCTION, or NULL */
  PyObject *annotations_cache; /* realized __annotations__ dict, or NULL */
  PyObject *name_override;  /* __name__ metadata write (e.g. wraps), or NULL.
                               Display-only: self-binding keys off `name` */
  PyObject *qualname_override; /* __qualname__ metadata write, or NULL */
  PyObject *module_override; /* __module__ metadata write, or NULL */
  int annotations_need_resolve; /* creation dict holds str forward refs */
  int simple_arity;         /* count of plain positional formals, or -1
                               when a positional-only, keyword-only,
                               star or by-ref formal makes the formals
                               not simple. Defaults do not matter: the
                               simple paths bind a call passing exactly
                               this many arguments by slot, and no
                               default is consulted at full supply */
  int install_arity;        /* simple_arity, or -1 for a generator
                               function, whose call creates a record
                               instead of installing a frame. The one
                               value the install arms compare nargs
                               against; -1 matches no call */
  int self_slot;            /* fast slot of the function's own name in
                               its frame, or -1 when the body does not
                               name it (locals head the fast names in
                               declaration order). Copied from the
                               spec; see KosFunction_StoreSelf */
  KosDynFrame *cached_frame; /* head of the cached prepared frames
                                (code + reset ns + cleared stack, fn
                                NULL) for the next calls; owned;
                                NULL when empty. Linked through each
                                frame's back_frame, newest first, with
                                the list length in the head's depth
                                field; capped at KOS_FRAME_CACHE_MAX.
                                See KosFunction_CacheFrame */
  PyObject *dead_record;    /* one finished Coroutine or AsyncGenerator
                               of this function, stored by its dealloc:
                               untracked, every field clear, refcount
                               zero as on a freelist; the function's
                               next call revives it (coroutine_new_bound,
                               KosPyAsyncGen_New) and the function's
                               release frees it. A function's code
                               either yields or not, so the slot only
                               ever sees one kind. NULL when empty */
} KosFunction;

/* Cached frames a function keeps. One serves a non-recursive function;
   a recursion of depth d finds the cache empty on the way down and
   caches d frames on the way up, so the next descent is served up to
   this depth. */
#define KOS_FRAME_CACHE_MAX 8

#define KosFunction_SIMPLE_ARITY(fn) (((KosFunction *) (fn))->simple_arity)
#define KosFunction_INSTALL_ARITY(fn) (((KosFunction *) (fn))->install_arity)
#define KosFunction_SELF_SLOT(fn) (((KosFunction *) (fn))->self_slot)

/* Bind the function into its own frame when its body names it: a
   store into self_slot, a raw local, which cannot fail. A no-op for
   every other function, so every call path runs it after binding the
   arguments. */
static inline void KosFunction_StoreSelf(PyObject *self, PyObject *frame_ns) {
  KosFunction *fn = (KosFunction *) self;
  if (fn->self_slot >= 0)
    (void) kos_ns_fast_store(frame_ns, (size_t) fn->self_slot, self);
}

#define KosFunction_CACHED_FRAME(fn) (((KosFunction *) (fn))->cached_frame)
#define KosFunction_DEAD_RECORD(fn) (((KosFunction *) (fn))->dead_record)


/**
   Unlink and return the head of the function's cached frames, or NULL.
   The ready-frame core takes the frame here before binding into it,
   since a binder can run user code and a call of this function from
   there must not take the frame being bound; a failed bind offers it
   back through KosFunction_CacheFrame. The install that follows
   overwrites the returned frame's back_frame and depth.
*/
static inline KosDynFrame *KosFunction_PopCachedFrame(PyObject *self) {
  KosFunction *fn = (KosFunction *) self;
  KosDynFrame *head = fn->cached_frame;

  if (head) {
    /* every cached frame's depth is one more than its successor's,
       set when it was cached, so next already carries its own */
    fn->cached_frame = (KosDynFrame *) KosDynFrame_BACK_FRAME(head);
    KosDynFrame_BACK_FRAME(head) = NULL;
  }
  return head;
}


/**
   Offer a released frame to the function's frame cache.
   The frame is cached only when the cache is under its cap and the
   frame is provably private: exact Namespace with refcount 1,
   GC-untracked, no binds dict, and no native handler.  On caching
   the namespace's raw slots are cleared, the segment is cleared, and
   the node base is normalized for kos_dyn_push_ready_frame.  The
   frame's fn field must already be NULL (kos_dynframe_free clears it
   before offering).

   :return: 1 when cached (ownership taken), 0 when refused
*/
int KosFunction_CacheFrame(PyObject *self, KosDynFrame *frame);


/** Borrowed reference. Do not Py_DECREF the result. */
#define KosFunction_CODE(func) (((KosFunction *) (func))->code)

#define KosFunction_NAME(func) (((KosFunction *) (func))->name)

#define KosFunction_BASE_NS(func) (((KosFunction *) (func))->base_ns)

#define KosFunction_FORMALS(func) (((KosFunction *) (func))->formals)

#define KosFunction_DOC(func) (((KosFunction *) (func))->doc)

#define KosFunction_DICT(func) (((KosFunction *) (func))->dict)

#define KosFunction_CREATION_ANNOTATIONS(func)          \
  (((KosFunction *) (func))->creation_annotations)

#define KosFunction_VECTORCALL_OFFSET offsetof(KosFunction, vectorcall)

#define KosFunction_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosFunctionType)))


/* === KosBoundMethod === */


extern KosEvaluatedTypeObject KosBoundMethodType;


typedef struct {
  PyObject_HEAD
  PyObject *func;
  PyObject *self;
  vectorcallfunc vectorcall; /* for Py_TPFLAGS_HAVE_VECTORCALL; always set */
} KosBoundMethod;


#define KosBoundMethod_FUNC(bm) (((KosBoundMethod *) (bm))->func)
#define KosBoundMethod_SELF(bm) (((KosBoundMethod *) (bm))->self)

#define KosBoundMethod_VECTORCALL_OFFSET offsetof(KosBoundMethod, vectorcall)

#define KosBoundMethod_CheckExact(obj)                           \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosBoundMethodType)))


/**
   Create a KosBoundMethod (func, self). func may be KosFunction or Demand.
   Used by KosFunction.__get__ and Demand.__get__ when accessed via instance.
   Caller must Py_DECREF when done.
*/
PyObject *KosBoundMethod_New(PyObject *func, PyObject *self);


/**
   Create a Function from (code, formals, parent_ns). Used by
   Code.function(formals, parent_ns). Same assembly as sibilant-ceval
   ByteCode.function(formals, parent); calling the result returns a
   Coroutine.

   :param code: KosCode instance
   :param formals: KosFormals instance (resolved from FormalsSpec + defaults)
   :param parent_ns: KosNamespace instance (base namespace)
   :return: new Function, or NULL on error
*/
PyObject *KosFunction_NewFromFormals(
      PyObject *code,
      PyObject *formals,
      PyObject *parent_ns);


/**
   Call the function with no arguments.
*/
PyObject *KosFunction_CallNoArgs(PyObject *func);


/**
   Call the function with the given args and kwds.
*/
PyObject *KosFunction_Call(
      PyObject *func,
      PyObject *args,
      PyObject *kwds);


/**
   Call the function using the vectorcall convention, returning a new Coroutine.
   nargsf encodes the positional count (use PyVectorcall_NARGS to extract).
   kwnames is a tuple of keyword names corresponding to values at args[npos..].
*/
PyObject *KosFunction_VectorCall(
      PyObject *func,
      PyObject *const *args,
      size_t nargsf,
      PyObject *kwnames);


/**
   The install slot of an exact KosFunction: binds args into a call
   namespace and installs the body as a frame, or completes a
   generator function in hand with its AsyncGenerator. Reads argv in
   place. Reported through the install contract (evaluated.h).
*/
KosOpResult KosFunction_Install(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   Return the function's code (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosFunction_GetCode(PyObject *func);


/* === The ready frame: one bound root frame for any purpose ===

   Every call of a KosFunction, wherever it comes from, needs the same
   thing: a root frame over a namespace with the arguments bound into
   it, tagged with the function so the frame returns to the function's
   cache at completion. The core below is the one answer. It takes the
   cache head when there is one, else a fresh frame over a template
   copy; binds by the shape of the arguments; tags; and on a failed
   bind hands the frame back through the cache offer, which resets the
   namespace, so no caller ever sees a half-bound frame. The frame is
   ready for kos_dyn_push_ready_frame: parent NULL, depth 1, pc at the
   code's entry, stack empty.

   What the frame becomes is the caller's last step and nothing else:
   pushed on the chain (the call core, KosFunction_Install), held by a
   Coroutine (the Python call entries), or parented under a new GEN
   (KosAsyncGen_NewFromFrame) when the code yields. The core itself
   knows nothing of contexts, chains or coroutines.

   Three argument sources, one entry each so the bind folds at the
   site:

   - KosFunction_ReadyFrameBlock: the top nargs references of an
     operand segment, stolen, with an optional receiver for slot 0.
     The simple-call class only (plain positional formals at exact
     arity, classified by the caller); the one
     inline entry, since it is the evaluator's hottest call path.
   - KosFunction_ReadyFrameVector: a borrowed vector with kwnames, the
     vectorcall convention. The simple class binds by slot; anything
     else through the formals machine.
   - KosFunction_ReadyFrameTuple: a tuple and a dict, the tp_call
     convention, through the formals machine. */


/**
   A fresh frame over a copy of the function's template namespace: the
   cache miss. NULL with an error set.
*/
KosDynFrame *kos_function_frame_fresh(PyObject *self);


/**
   The frame the core starts from: the cache head, popped, else a
   fresh frame (kos_function_frame_fresh, out of line: the hit is the
   hot path). *cached reports which, for the event counts. NULL with
   an error set.
*/
KOS_OP_INLINE KosDynFrame *kos_function_frame_take(
    PyObject *self,
    int *cached) {

  KosDynFrame *frame = KosFunction_PopCachedFrame(self);

  if (likely(frame != NULL)) {
    *cached = 1;
    return frame;
  }
  *cached = 0;
  return kos_function_frame_fresh(self);
}


/**
   A bind into the taken frame failed: the frame goes back through
   the cache offer, which resets the namespace to the template, or is
   freed when the cache refuses it. The frame is untagged at this
   point, as the offer requires.
*/
void kos_function_frame_return(PyObject *self, KosDynFrame *frame);


/** The bound frame's tag: the function, a new reference unless the
    caller's owned reference moves in (self_owned). */
static inline void kos_function_frame_tag(
    PyObject *self,
    int self_owned,
    KosDynFrame *frame) {

  if (! self_owned)
    Py_INCREF(self);
  KosDynFrame_FN(frame) = self;
}


/**
   The ready frame for a simple call whose arguments sit at the tail of
   block: the top nargs references move into fast slots, slot 0 from
   recv when one is given (a reference taken when recv_owned is 0, the
   caller's moved when it is 1), and the block length retracts in the
   same step. The caller has classified the call (exact KosFunction
   with install arity nargs, or nargs + 1 with recv, which excludes a
   generator function) and has asked kos_eval_check_frame_room when it
   means to push. With self_owned the caller's reference to the
   function moves into the frame's tag on success; otherwise the tag
   takes its own. On failure nothing is stolen, recv and the caller's
   references are untouched, and NULL is returned with the error set.
*/
KOS_OP_INLINE KosDynFrame *KosFunction_ReadyFrameBlock(
    PyObject *self,
    int self_owned,
    KosSegment *block,
    PyObject *recv,
    int recv_owned,
    Py_ssize_t nargs) {

  int cached;
  KosDynFrame *frame = kos_function_frame_take(self, &cached);
  PyObject *ns;

  if (unlikely(! frame))
    return NULL;
  ns = KosDynFrame_NS(frame);
  if (unlikely((recv
                ? KosFormals_SimpleStealApplySelfBlock(ns, recv, recv_owned,
                                                       block, nargs)
                : KosFormals_SimpleStealApplyBlock(ns, block, nargs)) < 0)) {
    kos_function_frame_return(self, frame);
    return NULL;
  }
  KosFunction_StoreSelf(self, ns);
  kos_function_frame_tag(self, self_owned, frame);
  KOS_TRACE_EVENTS_INC(cached ? KOS_TE_CALL_FRAME_CACHED
                              : KOS_TE_CALL_SIMPLE_STEAL);
  return frame;
}


/**
   The formals-machine half of KosFunction_ReadyFrameVector: frame is
   the taken frame, bound through KosFormals_VectorApplyFast and the
   self store, tagged on success and returned through the cache
   offer on failure. Out of line: the binder is the cold shape.
*/
KosDynFrame *kos_function_ready_frame_general(
    PyObject *self,
    KosDynFrame *frame,
    int cached,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames);


/**
   The ready frame for a call in the vectorcall convention: args holds
   nargs positionals then the keyword values kwnames names (NULL for
   none), all borrowed. The simple class binds by slot here, after
   taking a reference to each argument, since the steal binder moves
   pointers; any other shape binds through the formals machine
   (kos_function_ready_frame_general), which can run user code and can
   fail midway, with the frame then returned through the cache offer.
   NULL with the error set on failure.
*/
KOS_OP_INLINE KosDynFrame *KosFunction_ReadyFrameVector(
    PyObject *self,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  int cached;
  KosDynFrame *frame = kos_function_frame_take(self, &cached);
  PyObject *ns;

  if (unlikely(! frame))
    return NULL;
  if (kwnames || nargs != KosFunction_SIMPLE_ARITY(self))
    return kos_function_ready_frame_general(self, frame, cached, args, nargs,
                                            kwnames);

  ns = KosDynFrame_NS(frame);
  for (Py_ssize_t i = 0; i < nargs; i++)
    Py_INCREF(args[i]);
  (void) KosFormals_SimpleStealApply(ns, args, nargs);
  KosFunction_StoreSelf(self, ns);
  kos_function_frame_tag(self, 0, frame);
  KOS_TRACE_EVENTS_INC(cached ? KOS_TE_CALL_FRAME_CACHED
                              : KOS_TE_CALL_SIMPLE_VECTOR);
  return frame;
}


/**
   The ready frame for a call in the tp_call convention: args a tuple,
   kwds a dict or NULL, borrowed, bound through the formals machine.
   NULL with the error set on failure.
*/
KosDynFrame *KosFunction_ReadyFrameTuple(
    PyObject *self,
    PyObject *args,
    PyObject *kwds);


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod);
int koskari_func_clear(PyObject *mod);


#endif


// The end.
