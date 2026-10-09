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
  int bind_self_state;      /* self-bind on call: 0 unresolved (lazily
                               computed on first call; name is fixed after
                               construction), 1 bind, 2 no-bind. Only the
                               named-lambda form declares the function's
                               own name as a body local, so 1 is that
                               form recursing through its name; define
                               and function refer to the outer binding
                               and resolve to 2 */
  Py_ssize_t self_slot;     /* fast slot of the function's own name in its
                               frame, set when bind_self_state resolves to
                               1 (locals head the fast names in declaration
                               order); -1 otherwise. A call arm stores the
                               function there after binding the arguments,
                               see KosFunction_StoreSelf */
  Py_ssize_t simple_arity1; /* exact positional arity PLUS ONE for the
                               simple-call fast path (plain positional
                               formals, no defaults/star/kwd/by-refs);
                               a property of the formals alone, so the
                               call arms test the code for yields
                               themselves; 0 = not simple, so zeroed
                               construction is fail-safe */
  KosDynFrame *cached_frame; /* head of the cached prepared frames
                                (code + reset ns + cleared stack, fn
                                NULL) for the next simple calls; owned;
                                NULL when empty. Linked through each
                                frame's back_frame, newest first, with
                                the list length in the head's depth
                                field; capped at KOS_FRAME_CACHE_MAX.
                                See KosFunction_CacheFrame */
} KosFunction;

/* Cached frames a function keeps. One serves a non-recursive function;
   a recursion of depth d finds the cache empty on the way down and
   caches d frames on the way up, so the next descent is served up to
   this depth. */
#define KOS_FRAME_CACHE_MAX 8

#define KosFunction_SIMPLE_ARITY1(fn) (((KosFunction *) (fn))->simple_arity1)

/* Bind the function into its own frame when its body names it: a
   store into self_slot, a raw local, which cannot fail. A no-op for
   every other function, so every positional call arm runs it after
   binding the arguments; the arm requires only a resolved state. */
static inline void KosFunction_StoreSelf(PyObject *self, PyObject *frame_ns) {
  KosFunction *fn = (KosFunction *) self;
  if (fn->bind_self_state == 1)
    (void) kos_ns_fast_store(frame_ns, (size_t) fn->self_slot, self);
}

#define KosFunction_BIND_SELF_STATE(fn) (((KosFunction *) (fn))->bind_self_state)

#define KosFunction_CACHED_FRAME(fn) (((KosFunction *) (fn))->cached_frame)


/**
   Unlink and return the head of the function's cached frames, or NULL.
   A hit arm reads the head, steal-binds into its namespace, and calls
   this only once the bind succeeded, so a failed bind leaves the frame
   cached. The install that follows overwrites the returned frame's
   back_frame and depth.
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


/**
   Simple-call namespace prep: copy the base namespace and bind exactly
   nargs positionals by slot index, plus the cached self-bind.  Caller
   must have matched fn->simple_arity == nargs (classification
   guarantees plain positional formals; yielding code classifies too
   and takes the generator gate).

   :return: new namespace reference, or NULL with error set
*/
PyObject *KosFunction_SimpleCallNamespaceFromVector(
      PyObject *self,
      PyObject *const *args,
      Py_ssize_t nargs);


/**
   Steal-binding variant of the simple-call namespace prep: the top
   nargs references move off the block tail into the call namespace via
   KosFormals_SimpleStealApplyBlock, retracting the block length.
   Caller must have checked that bind_self_state is resolved; the
   self store that follows the steal cannot fail, so the post-copy
   path is infallible: a NULL return always means the arg span is
   still intact on the block.

   :return: new namespace reference, or NULL with error set
*/
PyObject *KosFunction_SimpleCallNamespaceStealBlock(
      PyObject *self,
      KosSegment *block,
      Py_ssize_t nargs);


/**
   Self-injecting variant of KosFunction_SimpleCallNamespaceStealBlock:
   slot 0 binds from recv (borrowed; a reference is taken) and the top
   nargs block references steal into slots 1..nargs.  Same contract; the
   function's simple arity is nargs + 1 and recv never lives on the
   block, so the caller's window accounting is untouched.

   :return: new namespace reference, or NULL with error set
*/
PyObject *KosFunction_SimpleCallNamespaceStealSelfBlock(
      PyObject *self,
      PyObject *recv,
      KosSegment *block,
      Py_ssize_t nargs);


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
} KosBoundMethod;


#define KosBoundMethod_FUNC(bm) (((KosBoundMethod *) (bm))->func)
#define KosBoundMethod_SELF(bm) (((KosBoundMethod *) (bm))->self)

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
   Prepare the call namespace for a KosFunction with args/kwds applied.
   Used by the evaluator to flatten Koskari calls without creating a coroutine.
   func, args, kwds are borrowed.
   Returns new reference to frame namespace, or NULL on error.
*/
PyObject *KosFunction_PrepareCallNamespace(
      PyObject *func,
      PyObject *args,
      PyObject *kwds);


/**
   Prepare the call namespace using the vectorcall convention. args is a
   C array of nargs positional arguments followed by keyword argument values;
   kwnames is a tuple of keyword names (may be NULL for positional-only).
   func, args, kwnames are borrowed.
   Returns new reference to frame namespace, or NULL on error.
*/
PyObject *KosFunction_PrepareCallNamespaceFromVector(
      PyObject *func,
      PyObject *const *args,
      Py_ssize_t nargs,
      PyObject *kwnames);


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
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   Return the function's code (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosFunction_GetCode(PyObject *func);


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod);
int koskari_func_clear(PyObject *mod);


#endif


// The end.
