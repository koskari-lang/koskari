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
#define PyObject_LookupAttr _PyObject_LookupAttr


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
static inline PyObject **kos_inline_value_slots(PyObject *obj, int *active) {
  KosDictOrValues312 dov = *kos_dict_or_values312(obj);
  if (! (((uintptr_t) dov.values) & 1)) {
    *active = 0;
    return NULL;
  }
  *active = 1;
  return ((KosDictValues312 *) (dov.values + 1))->values;
}


// The end.
