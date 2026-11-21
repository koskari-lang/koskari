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


#define PyDict_NewPresized _PyDict_NewPresized
#define PyGen_FetchStopIterationValue _PyGen_FetchStopIterationValue

/* PyUnicode_Equal public from 3.14 */
#define PyUnicode_Equal koskari_Unicode_Equal

/* PyErr_GetRaisedException added in 3.12 */
#define PyErr_GetRaisedException koskari_Err_GetRaisedException

/* PyObject_GetOptionalAttr public from 3.13; the private form has the
   same contract: 1 and *result set on a hit, 0 and NULL when the
   attribute is absent with no error raised, -1 with an error set */
#define PyObject_GetOptionalAttr _PyObject_LookupAttr

/* PyErr_SetHandledException added in 3.11: the three-part setter it
   replaced steals one reference per part */
__attribute__((always_inline))
static inline void PyErr_SetHandledException(PyObject *exc) {
  if (! exc) {
    PyErr_SetExcInfo(NULL, NULL, NULL);
    return;
  }
  Py_INCREF(Py_TYPE(exc));
  Py_INCREF(exc);
  PyErr_SetExcInfo((PyObject *) Py_TYPE(exc), exc,
                   PyException_GetTraceback(exc));
}


/* PyFrame_GetGlobals added in 3.11: a new reference to the frame's
   globals, read from the frame struct frameobject.h declares here */
#include <frameobject.h>

__attribute__((always_inline))
static inline PyObject *PyFrame_GetGlobals(PyFrameObject *frame) {
  return Py_NewRef(frame->f_globals);
}



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.10: an aggregate int on the interpreter's
   ceval state, with the GIL drop request beside it. Replicated under
   our own name (no internal header, no unexported symbol); compat.h
   builds the probes on these.
*/


/* struct _ceval_state and struct _is prefixes:
   https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_interp.h */
/* The pending-calls record through async_exc. Its leading lock gives
   the struct the pointer alignment the real one has, so it sits at
   the same offset after the interpreter prefix's final int. */
typedef struct {
  int recursion_limit;
  int eval_breaker;
  int gil_drop_request;
  struct {
    void *lock;
    int calls_to_do;
    int async_exc;
  } pending;
} KosCevalState310;

typedef struct {
  PyInterpreterState *next;
  PyThreadState *tstate_head;
  void *runtime;
  int64_t id;
  int64_t id_refcount;
  int requires_idref;
  void *id_mutex;
  int finalizing;
  KosCevalState310 ceval;
} KosInterpreterState310;

#define KOS_CEVAL_STATE(interp)                         \
  (&((KosInterpreterState310 *) (interp))->ceval)


/* === Recursion guard === */

/*
   CPython's C recursion count on 3.10: recursion_depth on the thread
   state against the limit on the interpreter's ceval state (the mirror
   above). The USE_STACKCHECK variant of the check is Windows-only and
   is not replicated.
*/


/* Count one level on tstate: 0 on success, -1 with an error set. The
   count and its test are _Py_EnterRecursiveCall's; a count past the
   limit is taken back and Py_EnterRecursiveCall repeats it, raising:
   https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_ceval.h */
static inline int kos_recursion_enter(
    PyThreadState *tstate,
    const char *where) {

  if (++tstate->recursion_depth
      > KOS_CEVAL_STATE(tstate->interp)->recursion_limit) {
    --tstate->recursion_depth;
    return Py_EnterRecursiveCall(where);
  }
  return 0;
}


/* Take back one level counted by kos_recursion_enter, as
   _Py_LeaveRecursiveCall does:
   https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_ceval.h */
static inline void kos_recursion_leave(PyThreadState *tstate) {
  tstate->recursion_depth--;
}


/* === GC tracked test === */

/* Whether the collector holds op: the _gc_next link word of the PyGC_Head
   that precedes a GC object, non-zero exactly while op is in a collector
   list. The same read as CPython's _PyObject_GC_IS_TRACKED, whose inline
   is not in any public header on this version; the head has been two
   link words ahead of the object since 3.8. Precondition: op's type is a
   GC type, as every caller here guarantees by construction. */
static inline int kos_gc_is_tracked(PyObject *op) {
  return ((const uintptr_t *) op)[-2] != 0;
}


/* Forget that op's finalizer has run: a record revived from a dead
   slot with _Py_NewReference keeps its PyGC_Head, whose finalized
   bit survives untrack and track, and PyObject_CallFinalizer skips an
   object with the bit set. The bit is the low bit of the _gc_prev link
   word, the second word of the head that precedes the object. */
static inline void kos_gc_clear_finalized(PyObject *op) {
  ((uintptr_t *) op)[-1] &= ~(uintptr_t) 1;  /* _PyGC_PREV_MASK_FINALIZED */
}


/* 1 when the exact generator gen carries CO_ITERABLE_COROUTINE (a
   types.coroutine marked generator function's product), which await
   takes as its own iterator. The code is reached through gi_code. */
static inline int kos_gen_is_iterable_coroutine(PyObject *gen) {
  PyCodeObject *code = (PyCodeObject *) ((PyGenObject *) gen)->gi_code;
  return (code->co_flags & CO_ITERABLE_COROUTINE) != 0;
}


/* The send protocol: PyIter_Send on this version. */
#define kos_iter_send PyIter_Send


/* Py_HashPointer is public from 3.13; before that the private name */
#define koskari_HashPointer _Py_HashPointer


/* === Type version tags === */


/* The type's version for a cache guard: 0 means no valid version, so
   an entry always carries a nonzero one and a cleared tag never
   matches it. Through 3.11 the tag is meaningful only while
   Py_TPFLAGS_VALID_VERSION_TAG is set. */
static inline uint64_t kos_type_version(PyTypeObject *tp) {
  if (! (tp->tp_flags & Py_TPFLAGS_VALID_VERSION_TAG))
    return 0;
  return (uint64_t) tp->tp_version_tag;
}


/* The type's version for a cache populate, 0 when it cannot be
   versioned. Through 3.11 a lookup during resolution has already
   assigned and flagged the tag. */
static inline uint64_t kos_type_version_assign(PyTypeObject *tp) {
  return kos_type_version(tp);
}


/* True when tp can gain no slot after creation: no attribute can be
   set on it and no instance can have its __class__ reassigned. Static
   types carry the flag from 3.10. */
static inline int kos_type_is_immutable(PyTypeObject *tp) {
  return PyType_HasFeature(tp, Py_TPFLAGS_IMMUTABLETYPE);
}


/* === PEP 634 type tests === */


/* 1 when obj's type is flagged a sequence, else 0; the flag decides it
   on this version, so it never raises. */
static inline int kos_type_is_sequence(PyObject *obj) {
  return PyType_HasFeature(Py_TYPE(obj), Py_TPFLAGS_SEQUENCE) ? 1 : 0;
}


/* 1 when obj's type is flagged a mapping, else 0, as above. */
static inline int kos_type_is_mapping(PyObject *obj) {
  return PyType_HasFeature(Py_TYPE(obj), Py_TPFLAGS_MAPPING) ? 1 : 0;
}


/* PEP 634's self-matching builtins and their subclasses: a single
   positional class sub-pattern matches the subject itself */
#define kos_type_matches_self(tp) \
  PyType_HasFeature((tp), _Py_TPFLAGS_MATCH_SELF)


/* === Handled exception === */


/* 1 when the thread state's top handled-exception item holds an
   exception, 0 otherwise; no references are taken. The top item,
   PyThreadState.exc_info, is the one PyErr_SetExcInfo writes; the
   items below it (a caller's handler, seen from inside a generator)
   are not consulted. The item's exc_value decides when it is set, and
   holds none when None; with no exc_value, an exc_type that is NULL or
   None holds none. kos_tstate_exc_top takes what this answers for. */
static inline int kos_tstate_exc_held(PyThreadState *tstate) {
  _PyErr_StackItem *item = tstate->exc_info;

  if (item->exc_value)
    return item->exc_value != Py_None;
  return item->exc_type && item->exc_type != Py_None;
}


/* New references to the exception the thread state's top
   handled-exception item holds, the item PyErr_SetExcInfo writes, or
   NULL in all three when kos_tstate_exc_held answers 0. Takes the
   item's exc_type, exc_value and exc_traceback as stored; the items
   below it are not consulted. */
static inline void kos_tstate_exc_top(
    PyThreadState *tstate,
    PyObject **type,
    PyObject **value,
    PyObject **tb) {

  _PyErr_StackItem *item = tstate->exc_info;

  if (! kos_tstate_exc_held(tstate)) {
    *type = NULL;
    *value = NULL;
    *tb = NULL;
    return;
  }

  *type = Py_XNewRef(item->exc_type);
  *value = Py_XNewRef(item->exc_value);
  *tb = Py_XNewRef(item->exc_traceback);
}


/* === StopIteration raise === */


/* Raise exc, a StopIteration instance, as CPython's generator send
   raises a return value on 3.10: _PyGen_SetStopIterationValue raises
   it through PyErr_SetObject, so the handled exception is chained onto
   its __context__.
   https://github.com/python/cpython/blob/v3.10.0/Objects/genobject.c
   Steals exc. */
static inline void kos_raise_stop_instance(PyObject *exc) {
  PyErr_SetObject(PyExc_StopIteration, exc);
  Py_DECREF(exc);
}


// The end.
