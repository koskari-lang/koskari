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


/* PyDict_NewPresized and PyGen_FetchStopIterationValue symbols may not be
   exported in 3.13+; use our wrappers. */
#define PyDict_NewPresized koskari_Dict_NewPresized
#define PyGen_FetchStopIterationValue koskari_FetchStopIterationValue

/* PyUnicode_Equal public from 3.14 */
#define PyUnicode_Equal koskari_Unicode_Equal

/* _PyList_Extend was removed in 3.13; PyList_Extend returns int.  Provide
   a wrapper that returns the list on success for code expecting the old API. */
PyObject *koskari_List_ExtendRet(PyObject *list, PyObject *iterable);
#define _PyList_Extend(list, iter) koskari_List_ExtendRet((PyObject *)(list), (iter))

/* _PyDictView_New was removed in 3.13.  Wrapper dispatches to items/keys. */
PyObject *koskari_DictView_New(PyObject *dict, PyTypeObject *type);
#define _PyDictView_New(dict, type) koskari_DictView_New((dict), (type))

/* _PyErr_FormatFromCause moved to Python's internal-only header in 3.13;
   extensions can no longer see it.  Provide our own copy. */
PyObject *koskari_Err_FormatFromCause(PyObject *exception,
                                       const char *format, ...);
#define _PyErr_FormatFromCause koskari_Err_FormatFromCause


/* === Inline-values instance attributes (3.13) === */


/*
   Value access for the instance-value field cache (KOS_FIELD_INSTANCE_VALUE).
   On 3.13 inline values are embedded at a fixed offset (sizeof(PyObject),
   asserted equal to tp_basicsize for such types), preceded by a small header
   whose `valid` byte falls to 0 when the instance materializes its dict. The
   shared name -> index scan and the field-cache logic live in compat.h and
   eval/eval_cache.c; here we only expose the two hooks the resolver calls.
   Replicated under our own name (no internal header, no unexported symbol).
*/


/* struct _dictvalues: a header then the inline value array. */
typedef struct {
  uint8_t capacity;
  uint8_t size;
  uint8_t embedded;
  uint8_t valid;
  PyObject *values[1];
} KosDictValues313;


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
   The inline value slots for obj and whether they are live (valid, i.e. not
   materialized to a dict). When inactive the returned pointer must not be
   dereferenced. Mirrors _PyObject_InlineValues for 3.13.
*/
static inline PyObject **kos_inline_value_slots(
    PyObject *obj, int *active, uint32_t *capacity) {
  KosDictValues313 *v = (KosDictValues313 *) ((char *) obj + sizeof(PyObject));
  *active = v->valid;
  /* 3.13 sizes each instance's values from the shared keys at creation
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
   3.13 the dict pointer sits three slots ahead of the object (a materialized
   dict may coexist with still-valid inline values, which it shares).
*/
static inline PyObject *kos_managed_dict_peek(PyObject *obj) {
  return *(((PyObject **) obj) - 3);
}



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.13: a bit set on the thread state
   (pycore_ceval.h names the bits). Replicated under our own name (no
   internal header, no unexported symbol); compat.h builds the probes
   on these.
*/


/* struct _ts through eval_breaker:
   https://github.com/python/cpython/blob/v3.13.0/Include/cpython/pystate.h */
typedef struct {
  PyThreadState *prev;
  PyThreadState *next;
  PyInterpreterState *interp;
  uintptr_t eval_breaker;
} KosThreadState313;

#define KOS_BREAKER_WORD(tstate)                        \
  (((KosThreadState313 *) (tstate))->eval_breaker)
#define KOS_BREAKER_GIL_DROP (1U << 0)  /* _PY_GIL_DROP_REQUEST_BIT */
#define KOS_BREAKER_SIGNALS  (1U << 1)  /* _PY_SIGNALS_PENDING_BIT */
#define KOS_BREAKER_GC_SCHEDULED (1U << 4)  /* _PY_GC_SCHEDULED_BIT */


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


/* The version of the extern defaults dict is a counter bumped by a dict
   watcher: ma_version_tag is deprecated on this version. */
#define KOS_MATCHER_DEFAULTS_WATCHER 1


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


// The end.
