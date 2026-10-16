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


/**
   koskari/lib/matcher.c

   The matcher subsystem: the Datum, DatumFrame and MatchCursor types,
   and the steps the MATCH_* instructions call, each one test or read
   over one element. A pattern is compiled to those instructions by
   koskari/match_code.py; nothing here walks a pattern
   (design/interpreter/matcher-bytecode.md).

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"


#include "matcher/datum.c"


/* === the tests === */


/**
   Interned "__class__" and object's own __class__ descriptor, created
   by koskari_matcher_init and released by koskari_matcher_clear.
*/
static PyObject *kos_class_name = NULL;
static PyObject *kos_object_class_descr = NULL;


/**
   Whether isinstance's virtual-subclass fallback can change a nominal
   miss for inst. object_isinstance reads inst.__class__ and accepts
   when that is a subtype of cls, so the miss is final when the read
   provably yields Py_TYPE(inst): the type resolves attributes
   generically and __class__ on its MRO is object's own data
   descriptor, which outranks any instance dict entry.
*/
static int matcher_nominal_miss_is_final(PyObject *inst) {

  PyTypeObject *tp = Py_TYPE(inst);

  if (tp->tp_getattro != PyObject_GenericGetAttr)
    return 0;
  if (! kos_object_class_descr)
    return 0;
  return PyType_Lookup(tp, kos_class_name) == kos_object_class_descr;
}


/**
   The isinstance answer when it is decided by the type walk alone: 1 or
   0, or -2 when deciding it would run user code (a metaclass
   __instancecheck__, or a __class__ read on a subject whose nominal miss
   is not final). cls is a type whose metaclass is exactly ``type``,
   which cannot carry a custom ``__instancecheck__``, so a nominal
   identity/MRO hit is the answer isinstance would give, and a nominal
   miss is final when matcher_nominal_miss_is_final proves the virtual
   ``__class__`` read cannot change it. The root pre-test uses this so
   the engine's own test is the one place such code runs.
*/
static int matcher_isinstance_pure(PyObject *inst, PyObject *cls) {
  if (likely(PyType_Check(cls)) && Py_TYPE(cls) == &PyType_Type) {
    if (PyObject_TypeCheck(inst, (PyTypeObject *) cls))
      return 1;
    if (matcher_nominal_miss_is_final(inst))
      return 0;
  }
  return -2;
}


/**
   isinstance for pattern tests: the type walk's answer where it has
   one, and PyObject_IsInstance where deciding runs user code.
*/
static int matcher_isinstance(PyObject *inst, PyObject *cls) {
  int r = matcher_isinstance_pure(inst, cls);
  if (r != -2)
    return r;
  return PyObject_IsInstance(inst, cls);
}


#ifndef Py_TPFLAGS_SEQUENCE
/**
   Cached collections.abc.Sequence, resolved lazily on the first
   sequence gate that the type alone cannot decide and then immortal:
   that path runs for every such element that flows past a sequence
   arm. Returns a borrowed reference, or NULL with an exception set.
*/
static PyObject *get_abc_sequence(void) {
  static PyObject *abc_sequence_type = NULL;

  if (! abc_sequence_type) {
    PyObject *abc = PyImport_ImportModule("collections.abc");
    if (! abc)
      return NULL;
    abc_sequence_type = PyObject_GetAttrString(abc, "Sequence");
    Py_DECREF(abc);
  }
  return abc_sequence_type;
}
#endif


#ifndef Py_TPFLAGS_MAPPING
/**
   Cached collections.abc.Mapping, resolved lazily on the first mapping
   gate that the type alone cannot decide and then immortal; a
   double-resolve race under the GIL at worst leaks one reference.
   Returns a borrowed reference, or NULL with an exception set.
*/
static PyObject *get_abc_mapping(void) {
  static PyObject *abc_mapping_type = NULL;

  if (! abc_mapping_type) {
    PyObject *abc = PyImport_ImportModule("collections.abc");
    if (! abc)
      return NULL;
    abc_mapping_type = PyObject_GetAttrString(abc, "Mapping");
    Py_DECREF(abc);
  }
  return abc_mapping_type;
}
#endif


/* The sequence-pattern gate on a peeled element (PEP 634), decided by
   the type alone: str, bytes and bytearray are atoms; the KosPair
   spine, list and tuple descend; any other type descends when
   sequence-flagged (3.10+). On 3.9 the answer for any other type is a
   collections.abc.Sequence isinstance, which reads __class__ and can run
   user code: -2, for the caller to take or leave. Returns 1 to descend,
   0 for no match, -2 undecided. */
static int matcher_sequence_gate_pure(PyObject *peeled) {
  if (PyUnicode_Check(peeled) || PyBytes_Check(peeled)
      || PyByteArray_Check(peeled))
    return 0;
  if (KosPair_Check(peeled) || KosNilP(peeled)
      || PyList_Check(peeled) || PyTuple_Check(peeled))
    return 1;
#ifdef Py_TPFLAGS_SEQUENCE
  return PyType_HasFeature(Py_TYPE(peeled), Py_TPFLAGS_SEQUENCE) ? 1 : 0;
#else
  return -2;
#endif
}


/* The sequence gate with the undecided case settled by the ABC
   isinstance. Mappings, sets and iterators are a soft no-match.
   Returns 1 to descend, 0 for no match, -1 on error. */
static int matcher_sequence_gate(PyObject *peeled) {
  int r = matcher_sequence_gate_pure(peeled);
#ifndef Py_TPFLAGS_SEQUENCE
  if (r == -2) {
    PyObject *seq_type = get_abc_sequence();
    if (! seq_type)
      return -1;
    return PyObject_IsInstance(peeled, seq_type);
  }
#endif
  return r;
}


/* The mapping-pattern gate on a peeled element (PEP 634), decided by
   the type alone: a dict, else a mapping-flagged type (3.10+). On 3.9
   any other type is undecided, -2, as for the sequence gate. */
static int matcher_mapping_gate_pure(PyObject *peeled) {
  if (PyDict_Check(peeled))
    return 1;
#ifdef Py_TPFLAGS_MAPPING
  return PyType_HasFeature(Py_TYPE(peeled), Py_TPFLAGS_MAPPING) ? 1 : 0;
#else
  return -2;
#endif
}


/* The mapping gate with the undecided case settled by the ABC
   isinstance. Returns 1 to descend, 0 for no match, -1 on error. */
static int matcher_mapping_gate(PyObject *peeled) {
  int r = matcher_mapping_gate_pure(peeled);
#ifndef Py_TPFLAGS_MAPPING
  if (r == -2) {
    PyObject *mapping_type = get_abc_mapping();
    if (! mapping_type)
      return -1;
    return PyObject_IsInstance(peeled, mapping_type);
  }
#endif
  return r;
}


/* Interned "__match_args__", created by koskari_matcher_init and
   released by koskari_matcher_clear. */
static PyObject *kos_match_args_name = NULL;


#include "matcher/step.c"


/* === unit lifecycle === */


int koskari_matcher_init(PyObject *mod) {
  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosDatumIterType))
    return -1;

  if (PyType_Ready(&KosDatumType))
    return -1;

  if (PyType_Ready(&KosDatumFrameType))
    return -1;

  if (PyType_Ready(&KosMatchCursorType))
    return -1;

  kos_match_args_name = PyUnicode_InternFromString("__match_args__");
  if (! kos_match_args_name)
    return -1;

  kos_class_name = PyUnicode_InternFromString("__class__");
  if (! kos_class_name)
    return -1;
  kos_object_class_descr = PyType_Lookup(
      &PyBaseObject_Type, kos_class_name);
  if (! kos_object_class_descr) {
    PyErr_SetString(PyExc_SystemError,
                    "object.__class__ descriptor not found");
    return -1;
  }
  Py_INCREF(kos_object_class_descr);

  if (PyDict_SetItemString(dict, "Datum",
                           (PyObject *) &KosDatumType))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(dict, "datum",
                           (PyObject *) &KosDatumType))
    return -1;

  if (PyDict_SetItemString(dict, "DatumFrame",
                           (PyObject *) &KosDatumFrameType))
    return -1;

  return 0;
}


int koskari_matcher_clear(PyObject *mod) {
  (void) mod;
  match_cursor_free_list_clear();
  Py_CLEAR(kos_match_args_name);
  Py_CLEAR(kos_class_name);
  Py_CLEAR(kos_object_class_descr);
  return 0;
}


// The end.
