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



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.10: an aggregate int on the interpreter's
   ceval state, with the GIL drop request beside it. Replicated under
   our own name (no internal header, no unexported symbol); compat.h
   builds the probes on these.
*/


/* struct _ceval_state and struct _is prefixes:
   https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_interp.h */
/* The trailing pending-calls lock gives the struct the pointer
   alignment the real one has, so it lands at the same offset after
   the interpreter prefix's final int. */
typedef struct {
  int recursion_limit;
  int eval_breaker;
  int gil_drop_request;
  void *pending_lock;
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


// The end.
