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


// The end.
