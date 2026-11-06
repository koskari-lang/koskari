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


/* _PyDict_NewPresized stays exported; PyGen_FetchStopIterationValue may
   not be, so that one goes through our wrapper. */
#define PyDict_NewPresized _PyDict_NewPresized
#define PyGen_FetchStopIterationValue koskari_FetchStopIterationValue

/* _PyList_Extend was removed in 3.14; PyList_Extend returns int.  Provide
   a wrapper that returns the list on success for code expecting the old API. */
PyObject *koskari_List_ExtendRet(PyObject *list, PyObject *iterable);
#define _PyList_Extend(list, iter) koskari_List_ExtendRet((PyObject *)(list), (iter))

/* _PyDictView_New was removed in 3.14.  Wrapper dispatches to items/keys. */
PyObject *koskari_DictView_New(PyObject *dict, PyTypeObject *type);
#define _PyDictView_New(dict, type) koskari_DictView_New((dict), (type))

/* _PyErr_FormatFromCause moved to Python's internal-only header in 3.13;
   extensions can no longer see it.  Provide our own copy. */
PyObject *koskari_Err_FormatFromCause(PyObject *exception,
                                       const char *format, ...);
#define _PyErr_FormatFromCause koskari_Err_FormatFromCause


/* === Inline-values instance attributes (3.14) === */


/*
   Value access for the instance-value field cache (KOS_FIELD_INSTANCE_VALUE).
   Same header-plus-array layout as 3.13, but 3.14 places the inline values at
   tp_basicsize (types with extra C fields may now carry them), so the offset
   is per-type rather than sizeof(PyObject). The `valid` byte falls to 0 on
   materialization. The shared name -> index scan and the field-cache logic
   live in compat.h and eval/eval_cache.c; here we only expose the two hooks
   the resolver calls. Replicated under our own name (no internal header, no
   unexported symbol).
*/


/* struct _dictvalues: a header then the inline value array. */
typedef struct {
  uint8_t capacity;
  uint8_t size;
  uint8_t embedded;
  uint8_t valid;
  PyObject *values[1];
} KosDictValues314;


/* Does tp store instance attributes as addressable inline values? The fast
   path is GIL-only; under free-threading the generic path stays correct. */
static inline int kos_type_has_inline_values(PyTypeObject *tp) {
#ifdef Py_GIL_DISABLED
  (void) tp;
  return 0;
#else
  return (tp->tp_flags & Py_TPFLAGS_INLINE_VALUES) != 0;
#endif
}


/*
   The inline value slots for obj and whether they are live (valid). A
   materialized dict may share valid values, so live does not mean no dict;
   a store that must notify the dict checks kos_managed_dict_peek. When
   inactive the returned pointer must not be dereferenced. Mirrors
   _PyObject_InlineValues for 3.14 (values at tp_basicsize).
*/
static inline PyObject **kos_inline_value_slots(
    PyObject *obj, int *active, uint32_t *capacity) {
  KosDictValues314 *v =
    (KosDictValues314 *) ((char *) obj + Py_TYPE(obj)->tp_basicsize);
  *active = v->valid;
  /* 3.14 sizes each instance's values from the shared keys at creation
     time, so a later-grown keys index can exceed this instance's
     buffer; consumers must bound their index by this capacity */
  *capacity = v->capacity;
  return v->values;
}


/* Does tp keep the instance dict in the managed pre-header slot? Peek is
   GIL-only: the free-threaded pre-header differs, so the generic (and
   possibly materializing) path stays correct there. */
static inline int kos_type_has_managed_dict(PyTypeObject *tp) {
#ifdef Py_GIL_DISABLED
  (void) tp;
  return 0;
#else
  return (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0;
#endif
}


/*
   The managed dict, or NULL when none has been materialized. A peek only:
   _PyObject_GetDictPtr materializes the dict of a values-mode instance as a
   side effect, which kills the inline-values fast paths for that object, so
   readers that merely want to consult the dict must come through here. On
   3.14 the dict pointer sits three slots ahead of the object (a materialized
   dict may coexist with still-valid inline values, which it shares).
*/
static inline PyObject *kos_managed_dict_peek(PyObject *obj) {
  return *(((PyObject **) obj) - 3);
}



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.14: a bit set on the thread state
   (pycore_ceval.h names the bits). Replicated under our own name (no
   internal header, no unexported symbol); compat.h builds the probes
   on these.
*/


/* struct _ts through eval_breaker:
   https://github.com/python/cpython/blob/v3.14.0/Include/cpython/pystate.h */
typedef struct {
  PyThreadState *prev;
  PyThreadState *next;
  PyInterpreterState *interp;
  uintptr_t eval_breaker;
} KosThreadState314;

#define KOS_BREAKER_WORD(tstate)                        \
  (((KosThreadState314 *) (tstate))->eval_breaker)
#define KOS_BREAKER_GIL_DROP (1U << 0)  /* _PY_GIL_DROP_REQUEST_BIT */
#define KOS_BREAKER_SIGNALS  (1U << 1)  /* _PY_SIGNALS_PENDING_BIT */
#define KOS_BREAKER_GC_SCHEDULED (1U << 4)  /* _PY_GC_SCHEDULED_BIT */


/* === Recursion guard === */

/*
   CPython's C recursion guard on 3.14: no count, a test of the machine
   stack address against a soft limit kept on the internal thread state
   record. Replicated under our own name (no internal header, no
   unexported symbol); the Python recursion limit stays in the public
   struct _ts.
*/


/* struct _PyThreadStateImpl through c_stack_hard_limit:
   https://github.com/python/cpython/blob/v3.14.0/Include/internal/pycore_tstate.h */
typedef struct {
  PyThreadState base;
  Py_ssize_t refcount;
  uintptr_t c_stack_top;
  uintptr_t c_stack_soft_limit;
  uintptr_t c_stack_hard_limit;
} KosThreadStateImpl314;


/* Test the C stack on tstate: 0 on success, -1 with an error set. The
   test is _Py_MakeRecCheck's; past the soft limit Py_EnterRecursiveCall
   repeats it, raising:
   https://github.com/python/cpython/blob/v3.14.0/Include/internal/pycore_ceval.h */
static inline int kos_recursion_enter(
    PyThreadState *tstate,
    const char *where) {

  if ((uintptr_t) __builtin_frame_address(0)
      < ((KosThreadStateImpl314 *) tstate)->c_stack_soft_limit)
    return Py_EnterRecursiveCall(where);
  return 0;
}


/* Nothing to take back: kos_recursion_enter counts nothing, as
   _Py_LeaveRecursiveCallTstate does nothing:
   https://github.com/python/cpython/blob/v3.14.0/Include/internal/pycore_ceval.h */
static inline void kos_recursion_leave(PyThreadState *tstate) {
  (void) tstate;
}


/* The recursion limit, the value Py_GetRecursionLimit returns:
   Py_SetRecursionLimit writes it to every thread state's
   py_recursion_limit:
   https://github.com/python/cpython/blob/v3.14.0/Python/ceval.c
   https://github.com/python/cpython/blob/v3.14.0/Include/cpython/pystate.h */
static inline int kos_recursion_limit(PyThreadState *tstate) {
  return tstate->py_recursion_limit;
}


/* === GC tracked test === */

/* Whether the collector holds op. The free-threaded build keeps the
   tracked bit in the object header; the default build keeps the object
   on a collector list, so the _gc_next link word of the PyGC_Head that
   precedes it is non-zero exactly while tracked. The same read as
   CPython's _PyObject_GC_IS_TRACKED, whose inline is internal on this
   version; the head has been two link words ahead of the object since
   3.8. Precondition: op's type is a GC type, as every caller here
   guarantees by construction. */
static inline int kos_gc_is_tracked(PyObject *op) {
#ifdef Py_GIL_DISABLED
  return (op->ob_gc_bits & 1u) != 0;   /* _PyGC_BITS_TRACKED */
#else
  return ((const uintptr_t *) op)[-2] != 0;
#endif
}


/* Forget that op's finalizer has run: a record revived from a dead
   slot with _Py_NewReference keeps its GC record, whose finalized bit
   survives untrack and track, and PyObject_CallFinalizer skips an
   object with the bit set. The free-threaded build keeps the bit in
   the object header; the default build keeps it as the low bit of the
   _gc_prev link word, the second word of the head that precedes the
   object. */
static inline void kos_gc_clear_finalized(PyObject *op) {
#ifdef Py_GIL_DISABLED
  op->ob_gc_bits &= ~2u;   /* _PyGC_BITS_FINALIZED */
#else
  ((uintptr_t *) op)[-1] &= ~(uintptr_t) 1;  /* _PyGC_PREV_MASK_FINALIZED */
#endif
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


/* Py_HashPointer is public from 3.13 */
#define koskari_HashPointer Py_HashPointer


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
   are not consulted. The item holds none when its exc_value is NULL;
   None is no longer stored there, and is taken as none as well.
   kos_tstate_exc_top takes what this answers for. */
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
   raises a return value on 3.14: _PyGen_SetStopIterationValue raises
   it through PyErr_SetRaisedException, which chains no __context__.
   https://github.com/python/cpython/blob/v3.14.0/Objects/genobject.c
   Steals exc. */
static inline void kos_raise_stop_instance(PyObject *exc) {
  PyErr_SetRaisedException(exc);
}


// The end.
