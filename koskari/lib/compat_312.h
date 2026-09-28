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


/* struct _ceval_state and struct _is prefixes (gc_opaque spans struct
   _gc_runtime_state):
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_ceval_state.h
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_interp.h
   https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_gc.h */
typedef struct {
  int eval_breaker;
  int gil_drop_request;
} KosCevalState312;

typedef struct {
  uint64_t next_unique_id;
  PyThreadState *head;
  long count;
  size_t stacksize;
} KosPyThreads312;

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
  unsigned char gc_opaque[240];
  PyObject *sysdict;
  PyObject *builtins;
  KosCevalState312 ceval;
} KosInterpreterState312;

#define KOS_CEVAL_STATE(interp)                         \
  (&((KosInterpreterState312 *) (interp))->ceval)


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


/* The version of the extern defaults dict is a counter bumped by a dict
   watcher: ma_version_tag is deprecated on this version. */
#define KOS_MATCHER_DEFAULTS_WATCHER 1

/* PyDict_SetItem takes its reference to the value before the key
   lookup, so a value borrowed from a fast slot needs no hold. */
#define KOS_DICT_STORE_HOLDS_SLOT_VALUE 0


// The end.
