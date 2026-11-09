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
   koskari/lib/evaluated.c

   KosEvaluatedType: the abstract marker root for extended type objects
   carrying in-eval interface suites. The suites themselves are defined
   and dispatched by their own units (callable.h for the call suite);
   this unit is only the shared substrate.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === KosEvaluatedMetaType === */


static PyObject *str_call_name = NULL;


/**
   Record how heap type t is called. INHERIT unless t is over an
   adopter whose static type is a KosCallable and t's __call__ is not
   that static type's own; then KOS for a KosCallable __call__ and PY
   for any other.
*/
static void evaluated_meta_classify(PyTypeObject *t) {

  uint8_t kind = KOS_CALL_KIND_INHERIT;

  if (str_call_name && KosEvaluated_CheckType(t)) {
    KosEvaluatedTypeObject *st = KosEvaluated_Resolve(t);

    if (st && st->kos_call.kc_vinstall) {
      PyObject *mine = PyType_Lookup(t, str_call_name);
      PyObject *base = PyType_Lookup(KOS_EVALUATED_PYTYPE(*st),
                                     str_call_name);

      if (mine != base)
        kind = KosCallable_Check(mine)
            ? KOS_CALL_KIND_KOS : KOS_CALL_KIND_PY;
    }
  }

  ((KosEvaluatedHeapTypeObject *) t)->kos_call_kind = kind;
}


/**
   Classify heap type t again, then each of its subclasses, which see
   a __call__ set or deleted on t through their MRO.
*/
static int evaluated_meta_reclassify(PyTypeObject *t) {

  PyObject *subs;
  Py_ssize_t i;

  evaluated_meta_classify(t);

  subs = PyObject_CallMethod((PyObject *) &PyType_Type, "__subclasses__",
                             "O", (PyObject *) t);
  if (! subs)
    return -1;

  for (i = 0; i < PyList_GET_SIZE(subs); i++) {
    PyObject *sub = PyList_GET_ITEM(subs, i);

    if (PyObject_TypeCheck(sub, &KosEvaluatedMetaType)
        && (((PyTypeObject *) sub)->tp_flags & Py_TPFLAGS_HEAPTYPE)
        && evaluated_meta_reclassify((PyTypeObject *) sub) < 0) {
      Py_DECREF(subs);
      return -1;
    }
  }

  Py_DECREF(subs);
  return 0;
}


static PyObject *evaluated_meta_new(
    PyTypeObject *meta,
    PyObject *args,
    PyObject *kwds) {

  PyObject *t = PyType_Type.tp_new(meta, args, kwds);

  if (t && PyObject_TypeCheck(t, &KosEvaluatedMetaType)
      && (((PyTypeObject *) t)->tp_flags & Py_TPFLAGS_HEAPTYPE))
    evaluated_meta_classify((PyTypeObject *) t);
  return t;
}


static int evaluated_meta_setattro(
    PyObject *self,
    PyObject *name,
    PyObject *value) {

  if (PyType_Type.tp_setattro(self, name, value) < 0)
    return -1;

  if (str_call_name && PyUnicode_Check(name)
      && PyUnicode_Compare(name, str_call_name) == 0
      && (((PyTypeObject *) self)->tp_flags & Py_TPFLAGS_HEAPTYPE))
    return evaluated_meta_reclassify((PyTypeObject *) self);
  return 0;
}


/**
   The metatype of KosEvaluatedType, handed on by PyType_Ready to every
   adopter. GC support, traverse, clear and dealloc are inherited from
   type by PyType_Ready.
*/
PyTypeObject KosEvaluatedMetaType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.EvaluatedType",
  .tp_basicsize = sizeof(KosEvaluatedHeapTypeObject),
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
  .tp_doc = "Metatype of the native types the evaluator dispatches to"
            " directly; records how a subclass overrides __call__",
  .tp_setattro = evaluated_meta_setattro,
  .tp_base = &PyType_Type,
  .tp_new = evaluated_meta_new,
};


/* === KosEvaluatedType === */


KosEvaluatedTypeObject KosEvaluatedType = {
  .type = {
    PyVarObject_HEAD_INIT(&KosEvaluatedMetaType, 0)
    .tp_name = "koskari.lib._KosEvaluated",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Internal base type for native types the evaluator"
              " dispatches to directly",
    .tp_new = NULL,
  },
  .kos_call = { 0 },
  .kos_iter = { 0 },
};


/* === Unit lifecycle === */


int koskari_evaluated_init(PyObject *mod) {

  (void) mod;

  str_call_name = PyUnicode_InternFromString("__call__");
  if (! str_call_name)
    return -1;

  if (PyType_Ready(&KosEvaluatedMetaType) < 0)
    return -1;
  return PyType_Ready(KOS_EVALUATED_PYTYPE(KosEvaluatedType));
}


int koskari_evaluated_clear(PyObject *mod) {
  (void) mod;
  Py_CLEAR(str_call_name);
  return 0;
}


// The end.
