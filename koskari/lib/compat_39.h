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


/* Py_NewRef / Py_XNewRef added in 3.10 */
__attribute__((always_inline))
static inline PyObject *Py_NewRef(PyObject *obj) {
  Py_INCREF(obj);
  return obj;
}


__attribute__((always_inline))
static inline PyObject *Py_XNewRef(PyObject *obj) {
  Py_XINCREF(obj);
  return obj;
}



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.9: an aggregate int on the interpreter's
   ceval state, with the GIL drop request beside it. Replicated under
   our own name (no internal header, no unexported symbol); compat.h
   builds the probes on these.
*/


/* struct _ceval_state and struct _is prefixes:
   https://github.com/python/cpython/blob/v3.9.0/Include/internal/pycore_interp.h */
/* The trailing pending-calls lock gives the struct the pointer
   alignment the real one has, so it lands at the same offset after
   the interpreter prefix's final int. */
typedef struct {
  int recursion_limit;
  int tracing_possible;
  int eval_breaker;
  int gil_drop_request;
  void *pending_lock;
} KosCevalState39;

typedef struct {
  PyInterpreterState *next;
  PyThreadState *tstate_head;
  void *runtime;
  int64_t id;
  int64_t id_refcount;
  int requires_idref;
  void *id_mutex;
  int finalizing;
  KosCevalState39 ceval;
} KosInterpreterState39;

#define KOS_CEVAL_STATE(interp)                         \
  (&((KosInterpreterState39 *) (interp))->ceval)


/* === Recursion guard === */

/*
   CPython's C recursion count on 3.9: recursion_depth on the thread
   state against the limit on the interpreter's ceval state (the mirror
   above). The USE_STACKCHECK variant of the check is Windows-only and
   is not replicated.
*/


/* Count one level on tstate: 0 on success, -1 with an error set. The
   count and its test are _Py_EnterRecursiveCall's; a count past the
   limit is taken back and Py_EnterRecursiveCall repeats it, raising:
   https://github.com/python/cpython/blob/v3.9.0/Include/internal/pycore_ceval.h */
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


/* Take back one level counted by kos_recursion_enter, clearing the
   overflow flag below the lower water mark, as _Py_LeaveRecursiveCall
   and _Py_RecursionLimitLowerWaterMark do:
   https://github.com/python/cpython/blob/v3.9.0/Include/internal/pycore_ceval.h */
static inline void kos_recursion_leave(PyThreadState *tstate) {
  int limit = KOS_CEVAL_STATE(tstate->interp)->recursion_limit;
  int low = limit > 200 ? limit - 50 : 3 * (limit >> 2);

  tstate->recursion_depth--;
  if (tstate->recursion_depth < low)
    tstate->overflowed = 0;
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


/* The send protocol of 3.10 (PyIter_Send) over the send method: a
   yield is PYGEN_NEXT with *result the value, a completion is
   PYGEN_RETURN with *result the StopIteration value (None when it
   carried none), anything else is PYGEN_ERROR with *result NULL and
   the exception set. */
typedef enum {
  PYGEN_RETURN = 0,
  PYGEN_ERROR = -1,
  PYGEN_NEXT = 1,
} PySendResult;

static inline PySendResult kos_iter_send(
    PyObject *iter, PyObject *value, PyObject **result) {
  PyObject *r;
  /* as PyIter_Send: a None send on an iterator is a step of the
     iteration protocol, so a plain iterator from __await__ needs no
     send method; anything else goes through send */
  if (value == Py_None && PyIter_Check(iter))
    r = Py_TYPE(iter)->tp_iternext(iter);
  else
    r = PyObject_CallMethod(iter, "send", "(O)", value);
  if (r) {
    *result = r;
    return PYGEN_NEXT;
  }
  /* an exhausted iterator may return NULL with no exception set; the
     fetch below reads that as a completion carrying None */
  if (PyErr_Occurred() && ! PyErr_ExceptionMatches(PyExc_StopIteration)) {
    *result = NULL;
    return PYGEN_ERROR;
  }
  if (_PyGen_FetchStopIterationValue(result) < 0) {
    *result = NULL;
    return PYGEN_ERROR;
  }
  if (! *result)
    *result = Py_NewRef(Py_None);
  return PYGEN_RETURN;
}


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
   set on it and no instance can have its __class__ reassigned. 3.9
   has no immutable heap types, so only a static type qualifies. */
static inline int kos_type_is_immutable(PyTypeObject *tp) {
  return ! (tp->tp_flags & Py_TPFLAGS_HEAPTYPE);
}


/* === PEP 634 type tests === */


/* collections.abc's class of that name, resolved on first use into
   *cache and then held: the type flags that decide a sequence or a
   mapping arrive in 3.10. Borrowed, or NULL with an exception set. */
static inline PyObject *kos_abc_class(const char *name, PyObject **cache) {
  if (! *cache) {
    PyObject *abc = PyImport_ImportModule("collections.abc");
    if (! abc)
      return NULL;
    *cache = PyObject_GetAttrString(abc, name);
    Py_DECREF(abc);
  }
  return *cache;
}


/* 1 when obj is a collections.abc.Sequence, else 0; -1 with an
   exception set. The isinstance reads __class__ and can run user
   code. */
static inline int kos_type_is_sequence(PyObject *obj) {
  static PyObject *sequence = NULL;
  PyObject *cls = kos_abc_class("Sequence", &sequence);

  if (! cls)
    return -1;
  return PyObject_IsInstance(obj, cls);
}


/* 1 when obj is a collections.abc.Mapping, as above. */
static inline int kos_type_is_mapping(PyObject *obj) {
  static PyObject *mapping = NULL;
  PyObject *cls = kos_abc_class("Mapping", &mapping);

  if (! cls)
    return -1;
  return PyObject_IsInstance(obj, cls);
}


/* 3.9 predates _Py_TPFLAGS_MATCH_SELF; the eleven types PEP 634 names,
   and their subclasses, bool through int */
static inline int kos_type_matches_self(PyTypeObject *tp) {
  return PyType_IsSubtype(tp, &PyLong_Type)
      || PyType_IsSubtype(tp, &PyFloat_Type)
      || PyType_IsSubtype(tp, &PyUnicode_Type)
      || PyType_IsSubtype(tp, &PyBytes_Type)
      || PyType_IsSubtype(tp, &PyByteArray_Type)
      || PyType_IsSubtype(tp, &PyList_Type)
      || PyType_IsSubtype(tp, &PyTuple_Type)
      || PyType_IsSubtype(tp, &PyDict_Type)
      || PyType_IsSubtype(tp, &PySet_Type)
      || PyType_IsSubtype(tp, &PyFrozenSet_Type);
}


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
   raises a return value on 3.9: _PyGen_SetStopIterationValue raises
   it through PyErr_SetObject, so the handled exception is chained onto
   its __context__.
   https://github.com/python/cpython/blob/v3.9.0/Objects/genobject.c
   Steals exc. */
static inline void kos_raise_stop_instance(PyObject *exc) {
  PyErr_SetObject(PyExc_StopIteration, exc);
  Py_DECREF(exc);
}


// The end.
