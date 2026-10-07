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


/* The version of the extern defaults dict is its ma_version_tag: a
   global counter stamped on the dict at creation and at every mutation,
   so a reset's new dict reads different too. No dict watcher here, and
   none is needed for this. */
#define KOS_MATCHER_DEFAULTS_WATCHER 0

static inline uint64_t kos_matcher_defaults_dict_version(PyObject *dict) {
  return ((PyDictObject *) dict)->ma_version_tag;
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


// The end.
