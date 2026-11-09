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


// In 3.11+ the PyFrameObject structure becomes opaque. However we
// need to be able get and set f_back and f_lineno, so I've copied the
// structure into this header.
struct _frame {
  PyObject_HEAD

  PyFrameObject *f_back;
  struct _PyInterpreterFrame *f_frame;
  PyObject *f_trace;
  int f_lineno;

  // we don't actually need anything below this point, and I don't
  // want to chase their structure changes for fields I am not using,
  // so it's void pointers the rest of the way down.
  void *opaque [1];
};


/* === Inline-values instance attributes (3.11) === */


/*
   Value access for the instance-value field cache (KOS_FIELD_INSTANCE_VALUE).
   3.11 introduced the managed dict: an object's inline values hang off a
   pre-header slot at obj[-4] (the dict slot at obj[-3]), and that values
   pointer is NULL once the instance materializes its dict. The shared name ->
   index scan and the field-cache logic live in compat.h and eval/eval_cache.c;
   here we only expose the two hooks the resolver calls. CPython keeps these
   accessors internal and unexported, so we replicate the layout under our own
   name (no internal header, no unexported symbol).
*/


/* struct _dictvalues: the inline values trail an insertion-order prefix. */
typedef struct {
  PyObject *values[1];
} KosDictValues311;


/* Does tp store instance attributes as addressable inline values? */
static inline int kos_type_has_inline_values(PyTypeObject *tp) {
  return (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
}


/*
   The inline value slots for obj and whether they are live (not materialized
   to a dict). When inactive the returned pointer must not be dereferenced.
   Mirrors _PyObject_ValuesPointer: the values pointer sits four slots ahead of
   the object and is NULL once the dict is materialized.
*/
static inline PyObject **kos_inline_value_slots(
    PyObject *obj, int *active, uint32_t *capacity) {
  KosDictValues311 *v = *(((KosDictValues311 **) obj) - 4);
  if (! v) {
    *active = 0;
    *capacity = 0;
    return NULL;
  }
  *active = 1;
  /* every 3.11 instance's values buffer is sized to the keys object's
     full capacity, so any keys-derived index is in range */
  *capacity = UINT32_MAX;
  return v->values;
}


/* Does tp keep the instance dict in the managed pre-header slot? */
static inline int kos_type_has_managed_dict(PyTypeObject *tp) {
  return (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
}


/*
   The managed dict, or NULL when none has been materialized. A peek only:
   _PyObject_GetDictPtr materializes the dict of a values-mode instance as a
   side effect, which kills the inline-values fast paths for that object, so
   readers that merely want to consult the dict must come through here. On
   3.11 the dict pointer sits three slots ahead of the object, beside the
   values pointer at obj[-4].
*/
static inline PyObject *kos_managed_dict_peek(PyObject *obj) {
  return *(((PyObject **) obj) - 3);
}



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.11: an aggregate int on the interpreter's
   ceval state, with the GIL drop request beside it. Replicated under
   our own name (no internal header, no unexported symbol); compat.h
   builds the probes on these.
*/


/* struct _ceval_state and struct _is prefixes:
   https://github.com/python/cpython/blob/v3.11.0/Include/internal/pycore_interp.h */
/* The trailing pending-calls lock gives the struct the pointer
   alignment the real one has, so it lands at the same offset after
   the interpreter prefix's final _Bool. */
typedef struct {
  int recursion_limit;
  int eval_breaker;
  int gil_drop_request;
  void *pending_lock;
} KosCevalState311;

typedef struct {
  uint64_t next_unique_id;
  PyThreadState *head;
  long count;
  size_t stacksize;
} KosPyThreads311;

typedef struct {
  PyInterpreterState *next;
  KosPyThreads311 threads;
  void *runtime;
  int64_t id;
  int64_t id_refcount;
  int requires_idref;
  void *id_mutex;
  int _initialized;
  int finalizing;
  _Bool _static;
  KosCevalState311 ceval;
} KosInterpreterState311;

#define KOS_CEVAL_STATE(interp)                         \
  (&((KosInterpreterState311 *) (interp))->ceval)


/* === Recursion guard === */

/*
   CPython's C recursion count on 3.11: recursion_remaining on the
   thread state, counted down toward zero, and the limit beside it in
   the public struct _ts.
*/


/* Count one level on tstate: 0 on success, -1 with an error set. The
   count and its test are _Py_MakeRecCheck's; a count past the limit is
   taken back and Py_EnterRecursiveCall repeats it, raising:
   https://github.com/python/cpython/blob/v3.11.0/Include/internal/pycore_ceval.h */
static inline int kos_recursion_enter(
    PyThreadState *tstate,
    const char *where) {

  if (tstate->recursion_remaining-- <= 0) {
    tstate->recursion_remaining++;
    return Py_EnterRecursiveCall(where);
  }
  return 0;
}


/* Take back one level counted by kos_recursion_enter, as
   _Py_LeaveRecursiveCallTstate does:
   https://github.com/python/cpython/blob/v3.11.0/Include/internal/pycore_ceval.h */
static inline void kos_recursion_leave(PyThreadState *tstate) {
  tstate->recursion_remaining++;
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
   are not consulted. The item holds none when its exc_value is NULL or
   None. kos_tstate_exc_top takes what this answers for. */
static inline int kos_tstate_exc_held(PyThreadState *tstate) {
  PyObject *exc = tstate->exc_info->exc_value;

  return exc && exc != Py_None;
}


/* New references to the exception the thread state's top
   handled-exception item holds, the item PyErr_SetExcInfo writes, or
   NULL in all three when kos_tstate_exc_held answers 0. The item
   stores only the instance; the type is the instance's and the
   traceback its __traceback__ (NULL when it has none), as
   PyErr_GetExcInfo answers. The items below it are not consulted. */
static inline void kos_tstate_exc_top(
    PyThreadState *tstate,
    PyObject **type,
    PyObject **value,
    PyObject **tb) {

  PyObject *exc = tstate->exc_info->exc_value;

  if (! kos_tstate_exc_held(tstate)) {
    *type = NULL;
    *value = NULL;
    *tb = NULL;
    return;
  }

  *type = Py_NewRef((PyObject *) Py_TYPE(exc));
  *value = Py_NewRef(exc);
  *tb = PyException_GetTraceback(exc);
}


/* === StopIteration raise === */


/* Raise exc, a StopIteration instance, as CPython's generator send
   raises a return value on 3.11: _PyGen_SetStopIterationValue raises
   it through PyErr_SetObject, so the handled exception is chained onto
   its __context__.
   https://github.com/python/cpython/blob/v3.11.0/Objects/genobject.c
   Steals exc. */
static inline void kos_raise_stop_instance(PyObject *exc) {
  PyErr_SetObject(PyExc_StopIteration, exc);
  Py_DECREF(exc);
}


// The end.
