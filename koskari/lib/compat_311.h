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
static inline PyObject **kos_inline_value_slots(PyObject *obj, int *active) {
  KosDictValues311 *v = *(((KosDictValues311 **) obj) - 4);
  if (! v) {
    *active = 0;
    return NULL;
  }
  *active = 1;
  return v->values;
}


// The end.
