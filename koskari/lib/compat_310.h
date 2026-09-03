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
#define PyObject_LookupAttr _PyObject_LookupAttr

/* PyErr_GetRaisedException added in 3.12 */
#define PyErr_GetRaisedException koskari_Err_GetRaisedException



/* === The eval breaker === */


/*
   CPython's eval breaker on 3.10: an aggregate int on the interpreter's
   ceval state, with the GIL drop request beside it. Replicated under
   our own name (no internal header, no unexported symbol); compat.h
   builds the probes on these.
*/


/* struct _ceval_state and struct _is prefixes:
   https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_interp.h */
typedef struct {
  int recursion_limit;
  int eval_breaker;
  int gil_drop_request;
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


// The end.
