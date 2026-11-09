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


/* PyGen_FetchStopIterationValue is public in 3.13+; 3.12 still uses _Py. */
#define PyDict_NewPresized _PyDict_NewPresized
#define PyGen_FetchStopIterationValue _PyGen_FetchStopIterationValue

/* PyUnicode_Equal public from 3.14 */
#define PyUnicode_Equal koskari_Unicode_Equal

/* PyObject_GetOptionalAttr public from 3.13; the private form has the
   same contract: 1 and *result set on a hit, 0 and NULL when the
   attribute is absent with no error raised, -1 with an error set */
#define PyObject_GetOptionalAttr _PyObject_LookupAttr


/* === Inline-values instance attributes (3.12) === */


/*
   Value access for the instance-value field cache (KOS_FIELD_INSTANCE_VALUE).
   On 3.12 the managed slot at obj[-3] is a tagged union: inline values or a
   materialized dict. The shared name -> index scan and the field-cache logic
   live in compat.h and eval/eval_cache.c; here we only expose the two hooks
   the resolver calls, so the version differences stay isolated. CPython keeps
   these accessors internal and unexported, so we replicate the layout under
   our own names (no internal header, no unexported symbol).
*/


/* struct _dictvalues: the inline values trail an insertion-order prefix. */
typedef struct {
  PyObject *values[1];
} KosDictValues312;


/* union { PyObject *dict; char *values; } at obj[-3] for a managed dict. */
typedef union {
  PyObject *dict;
  char *values;
} KosDictOrValues312;


/* The managed dict-or-values slot: three pointers ahead of the object. */
static inline KosDictOrValues312 *kos_dict_or_values312(PyObject *obj) {
  return ((KosDictOrValues312 *) obj) - 3;
}


/* Does tp store instance attributes as addressable inline values? */
static inline int kos_type_has_inline_values(PyTypeObject *tp) {
  return (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
}


/*
   The inline value slots for obj and whether they are live (not materialized
   to a dict). When inactive the returned pointer must not be dereferenced.
   The values tag is set by storing (real - 1), so the array is recovered as
   (stored + 1); mirrors _PyDictOrValues_GetValues.
*/
static inline PyObject **kos_inline_value_slots(
    PyObject *obj, int *active, uint32_t *capacity) {
  KosDictOrValues312 dov = *kos_dict_or_values312(obj);
  if (! (((uintptr_t) dov.values) & 1)) {
    *active = 0;
    *capacity = 0;
    return NULL;
  }
  *active = 1;
  /* every 3.12 instance's values buffer is sized to the keys object's
     full capacity, so any keys-derived index is in range */
  *capacity = UINT32_MAX;
  return ((KosDictValues312 *) (dov.values + 1))->values;
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
   3.12 the dict-or-values slot is tagged: a set low bit means live inline
   values (no dict), otherwise the union holds the dict pointer.
*/
static inline PyObject *kos_managed_dict_peek(PyObject *obj) {
  KosDictOrValues312 dov = *kos_dict_or_values312(obj);
  if (((uintptr_t) dov.values) & 1)
    return NULL;
  return dov.dict;
}



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.12: an aggregate int at the head of the
   interpreter's ceval state, with the GIL drop request beside it.
   Replicated under our own name (no internal header, no unexported
   symbol); compat.h builds the probes on these.
*/


/* struct _ceval_state through the pending-calls record's async_exc,
   struct _gc_runtime_state with the two records it holds, and the
   struct _is prefix, field for field with their own types so the
   layout is the compiler's arithmetic at any pointer width:
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_ceval_state.h
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_interp.h
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_gc.h */
typedef struct {
  int eval_breaker;
  int gil_drop_request;
  int recursion_limit;
  void *gil;
  int own_gil;
  int gc_scheduled;
  struct {
    int busy;
    void *lock;
    int calls_to_do;
    int async_exc;
  } pending;
} KosCevalState312;

typedef struct {
  uint64_t next_unique_id;
  PyThreadState *head;
  long count;
  size_t stacksize;
} KosPyThreads312;

typedef struct {
  uintptr_t head[2];     /* PyGC_Head */
  int threshold;
  int count;
} KosGcGeneration312;

typedef struct {
  Py_ssize_t collections;
  Py_ssize_t collected;
  Py_ssize_t uncollectable;
} KosGcGenerationStats312;

typedef struct {
  PyObject *trash_delete_later;
  int trash_delete_nesting;
  int enabled;
  int debug;
  KosGcGeneration312 generations[3];
  void *generation0;
  KosGcGeneration312 permanent_generation;
  KosGcGenerationStats312 generation_stats[3];
  int collecting;
  PyObject *garbage;
  PyObject *callbacks;
  Py_ssize_t long_lived_total;
  Py_ssize_t long_lived_pending;
} KosGcRuntimeState312;

typedef struct {
  PyInterpreterState *next;
  int64_t id;
  int64_t id_refcount;
  int requires_idref;
  void *id_mutex;
  int _initialized;
  int finalizing;
  uint64_t monitoring_version;
  uint64_t last_restart_version;
  KosPyThreads312 threads;
  void *runtime;
  uintptr_t _finalizing;
  KosGcRuntimeState312 gc;
  PyObject *sysdict;
  PyObject *builtins;
  KosCevalState312 ceval;
} KosInterpreterState312;

#define KOS_CEVAL_STATE(interp)                         \
  (&((KosInterpreterState312 *) (interp))->ceval)


/* === Recursion guard === */

/*
   CPython's C recursion count on 3.12: c_recursion_remaining on the
   thread state, counted down toward zero, with the Python recursion
   limit beside it in the public struct _ts.
*/


/* Count one level on tstate: 0 on success, -1 with an error set. The
   count and its test are _Py_MakeRecCheck's; a count past the limit is
   taken back and Py_EnterRecursiveCall repeats it, raising:
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_ceval.h */
static inline int kos_recursion_enter(
    PyThreadState *tstate,
    const char *where) {

  if (tstate->c_recursion_remaining-- <= 0) {
    tstate->c_recursion_remaining++;
    return Py_EnterRecursiveCall(where);
  }
  return 0;
}


/* Take back one level counted by kos_recursion_enter, as
   _Py_LeaveRecursiveCallTstate does:
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_ceval.h */
static inline void kos_recursion_leave(PyThreadState *tstate) {
  tstate->c_recursion_remaining++;
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
   takes as its own iterator. gi_code is gone from the struct; the
   code is reached through PyGen_GetCode, a new reference. */
static inline int kos_gen_is_iterable_coroutine(PyObject *gen) {
  PyCodeObject *code = PyGen_GetCode((PyGenObject *) gen);
  int marked = (code->co_flags & CO_ITERABLE_COROUTINE) != 0;
  Py_DECREF(code);
  return marked;
}


/* The send protocol: PyIter_Send on this version. */
#define kos_iter_send PyIter_Send


/* Py_HashPointer is public from 3.13; before that the private name */
#define koskari_HashPointer _Py_HashPointer


/* === Type version tags === */


/* The type's version for a cache guard: 0 means no valid version, so
   an entry always carries a nonzero one and a cleared tag never
   matches it. From 3.12 the validity flag is deprecated (3.14 stops
   setting it) and a nonzero tp_version_tag is itself the signal;
   PyType_Modified zeroes it on change. */
static inline uint64_t kos_type_version(PyTypeObject *tp) {
  return (uint64_t) tp->tp_version_tag;
}


/* The type's version for a cache populate, 0 when it cannot be
   versioned. From 3.12 a lookup assigns a tag only conditionally, so
   it is forced through PyUnstable_Type_AssignVersionTag; otherwise a
   guard could never match. */
static inline uint64_t kos_type_version_assign(PyTypeObject *tp) {
  if (! PyUnstable_Type_AssignVersionTag(tp))
    return 0;
  return (uint64_t) tp->tp_version_tag;
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
   raises a return value on the 3.12 running this build, read from
   Py_Version so a wheel built on other 3.12 headers still matches:
   from 3.12.9 _PyGen_SetStopIterationValue raises it through
   PyErr_SetRaisedException, which chains no __context__; 3.12.0
   through 3.12.8 raised through PyErr_SetObject and chained the
   handled exception.
   https://github.com/python/cpython/blob/v3.12.13/Objects/genobject.c
   https://github.com/python/cpython/blob/v3.12.0/Objects/genobject.c
   Steals exc. */
static inline void kos_raise_stop_instance(PyObject *exc) {
  if (Py_Version >= 0x030C09F0) {
    PyErr_SetRaisedException(exc);
  } else {
    PyErr_SetObject(PyExc_StopIteration, exc);
    Py_DECREF(exc);
  }
}


// The end.
