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


#ifndef KOSKARI_EVALUATED_H
#define KOSKARI_EVALUATED_H


#include <Python.h>


/**
   KosEvaluatedTypeObject: the extended type object for native types
   the evaluator dispatches to directly.

   Each in-eval interface family is a nullable suite pointer, mirroring
   CPython's tp_as_* design: the call suite (install slots; defined in
   callable.h) and the iter suite (step slots). A NULL suite means the
   type does not implement that family.

   Detection is a marker-base subtype check against KosEvaluatedType,
   whose tp_basicsize is sizeof(PyObject), so adopting it constrains no
   instance layout. Suite reads resolve through the first static type
   in the tp_base chain: a Python-level subclass of an adopter is a
   plain PyHeapTypeObject without the extended fields, and reading past
   its PyTypeObject would be garbage.
*/


struct KosCallSlots;
struct KosIterSlots;


typedef struct {
  PyTypeObject type;
  struct KosCallSlots *tp_kos_call;  /* NULL: not in-eval callable */
  struct KosIterSlots *tp_kos_iter;  /* NULL: not in-eval iterable */
} KosEvaluatedTypeObject;


extern KosEvaluatedTypeObject KosEvaluatedType;


#define KOS_EVALUATED_PYTYPE(typeobj) (&((typeobj).type))

#define KosEvaluated_Check(obj)                                         \
  ((obj) && PyType_IsSubtype(Py_TYPE(obj),                              \
                             KOS_EVALUATED_PYTYPE(KosEvaluatedType)))


/**
   Resolve a type to its extended type object: the type itself when
   static, else the first static type in its tp_base chain. Caller must
   have established KosEvaluated_Check on the instance; the walk body
   never runs for the static types on the hot paths.
*/
static inline KosEvaluatedTypeObject *KosEvaluated_Resolve(PyTypeObject *t) {

  while (t && (t->tp_flags & Py_TPFLAGS_HEAPTYPE))
    t = t->tp_base;
  return (KosEvaluatedTypeObject *) t;
}


static inline struct KosCallSlots *KosEvaluated_CallSlots(PyTypeObject *t) {

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(t);
  return et ? et->tp_kos_call : NULL;
}


static inline struct KosIterSlots *KosEvaluated_IterSlots(PyTypeObject *t) {

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(t);
  return et ? et->tp_kos_iter : NULL;
}


int koskari_evaluated_init(PyObject *mod);
int koskari_evaluated_clear(PyObject *mod);


#endif /* KOSKARI_EVALUATED_H */


// The end.
