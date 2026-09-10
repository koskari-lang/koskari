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


// The end.
