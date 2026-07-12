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
   The instance-value field cache (KOS_FIELD_INSTANCE_VALUE) reads and
   overwrites an object's inline attribute values directly. CPython keeps the
   accessors it would need (_PyDictKeys_StringLookup, the managed-dict helpers)
   internal and unexported, so we replicate just the layout we touch. These
   structs mirror Include/internal/pycore_{object,dict}.h for 3.12 ONLY; they
   are our own names (no internal header, no unexported symbol), read under a
   type-version guard at the call site. See eval/eval_cache.c.
*/


/* struct _dictvalues: the inline values trail an insertion-order prefix. */
typedef struct {
  PyObject *values[1];
} KosDictValues312;


/* The prefix of struct _dictkeysobject we read: the shared/cached keys. */
typedef struct {
  Py_ssize_t dk_refcnt;
  uint8_t dk_log2_size;
  uint8_t dk_log2_index_bytes;
  uint8_t dk_kind;
  uint32_t dk_version;
  Py_ssize_t dk_usable;
  Py_ssize_t dk_nentries;
  char dk_indices[];
} KosDictKeys312;


/* A unicode key entry (split/unicode tables store these). */
typedef struct {
  PyObject *me_key;
  PyObject *me_value;
} KosDictUnicodeEntry312;


/* union { PyObject *dict; char *values; } at obj[-3] for a managed dict. */
typedef union {
  PyObject *dict;
  char *values;
} KosDictOrValues312;


/*
   The entries follow the hash table (dk_indices), whose size in bytes is
   (1 << dk_log2_index_bytes). Mirrors _DK_ENTRIES for the unicode case.
*/
static inline KosDictUnicodeEntry312 *
kos_dk_unicode_entries312(KosDictKeys312 *dk) {
  size_t ix = (size_t) 1 << dk->dk_log2_index_bytes;
  return (KosDictUnicodeEntry312 *) (dk->dk_indices + ix);
}


/* The managed dict-or-values slot: three pointers ahead of the object. */
static inline KosDictOrValues312 *kos_dict_or_values312(PyObject *obj) {
  return ((KosDictOrValues312 *) obj) - 3;
}


/* Is the managed slot holding inline values (tag bit) or a real dict? */
static inline int kos_dov_is_values312(KosDictOrValues312 dov) {
  return ((uintptr_t) dov.values) & 1;
}


/* The inline values array. The tag is set by storing (real - 1), so the
   array is recovered as (stored + 1); mirrors _PyDictOrValues_GetValues. */
static inline KosDictValues312 *kos_dov_get_values312(KosDictOrValues312 dov) {
  return (KosDictValues312 *) (dov.values + 1);
}


// The end.
