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


#ifndef KOSKARI_REF_H
#define KOSKARI_REF_H


#include <Python.h>


/* === Ref (base symbolic reference) === */


extern PyTypeObject KosRefType;


typedef struct KosRef {
  PyObject_HEAD

  PyObject *name;
  PyObject *obj;
} KosRef;


PyObject *KosSymbolicRef_New(PyObject *name, PyObject *value);


#define KosSymbolicRef_VALUE(r)                 \
  (((KosRef *) r)->obj)

#define KosSymbolicRef_IS_EMPTY(r)              \
  (! KosSymbolicRef_VALUE(r))


/* === AttributeRef === */


extern PyTypeObject KosAttributeRefType;


typedef struct KosAttributeRef {
  KosRef super;
  PyObject *attr;
} KosAttributeRef;


PyObject *KosAttributeRef_New(PyObject *name,
                              PyObject *obj, PyObject *attr);


#define KosAttributeRef_OBJ(r)                  \
  (((KosRef *) r)->obj)

#define KosAttributeRef_ATTR(r)                 \
  (((KosAttributeRef *) r)->attr)


/* === ItemRef === */


extern PyTypeObject KosItemRefType;


typedef struct KosItemRef {
  KosRef super;
  PyObject *key;
} KosItemRef;


PyObject *KosItemRef_New(PyObject *name,
                         PyObject *obj, PyObject *key);


#define KosItemRef_OBJ(r)                  \
  (((KosRef *) r)->obj)

#define KosItemRef_KEY(r)                 \
  (((KosItemRef *) r)->key)


/* === DynamicRef === */


extern PyTypeObject KosDynamicRefType;


typedef struct KosDynamicRef {
  KosRef super;

  PyObject *getfn;
  PyObject *setfn;
  PyObject *delfn;
} KosDynamicRef;


#define KosDynamicRef_GETFN(r)                  \
  (((KosDynamicRef *) r)->getfn)

#define KosDynamicRef_SETFN(r)                  \
  (((KosDynamicRef *) r)->setfn)

#define KosDynamicRef_DELFN(r)                  \
  (((KosDynamicRef *) r)->delfn)

#define KosDynamicRef_EMPTYFN(r)                \
  (((KosRef *) r)->obj)


PyObject *KosDynamicRef_New(
    PyObject *name,
    PyObject *getfn,
    PyObject *setfn,
    PyObject *delfn,
    PyObject *emptyfn);


/* === ConstantRef === */


extern PyTypeObject KosConstantRefType;


typedef struct KosConstantRef {
  KosRef super;
} KosConstantRef;


PyObject *KosConstantRef_New(PyObject *name, PyObject *value);


/* === MemoizedRef === */


extern PyTypeObject KosMemoizedRefType;


typedef struct KosMemoizedRef {
  KosRef super;

  PyObject *getfn;
} KosMemoizedRef;


PyObject *KosMemoizedRef_New(PyObject *name, PyObject *getfn);


#define KosMemoizedRef_VALUE(r)                 \
  (((KosRef *) r)->obj)

#define KosMemoizedRef_GETFN(r)                 \
  (((KosMemoizedRef *) r)->getfn)


/* === ParamRef === */


extern PyTypeObject KosParamRefType;


typedef struct KosParamRef {
  KosRef super;
} KosParamRef;


#define KosParamRef_KEY(r)                      \
  (KosRef_NAME(r))

#define KosParamRef_DEFAULT(r)                  \
  (((KosRef *) r)->obj)


PyObject *KosParamRef_New(PyObject *key, PyObject *default_value);


/* === Generic Ref functions === */


#define KosRef_New(n, v)                        \
  KosSymbolicRef_New(n, v)


#define KosRef_NAME(r)                          \
  (((KosRef *) r)->name)


PyObject *KosRef_Name(PyObject *ref);


/**
   ref.deref()

   New reference or NULL with error set.
*/
PyObject *KosRef_Deref(PyObject *ref);


/**
   sets ref->value = val
   0 for success, -1 for error
 */
int KosRef_Store(PyObject *ref, PyObject *val);


/**
   sets ref->value to NULL
   0 for success, -1 for error
*/
int KosRef_Clear(PyObject *ref);


/**
   true if ref->value == NULL
   0 for non-empty, 1 for empty, -1 for error
*/
int KosRef_IsEmpty(PyObject *ref);


/* === Type check macros === */


#define KosRef_Check(obj)                                       \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosRefType))

#define KosSymbolicRef_CheckExact(obj)          \
  ((obj) && ((obj)->ob_type == &KosRefType))

#define KosAttributeRef_CheckExact(obj)                 \
  ((obj) && ((obj)->ob_type == &KosAttributeRefType))

#define KosItemRef_CheckExact(obj)                      \
  ((obj) && ((obj)->ob_type == &KosItemRefType))

#define KosDynamicRef_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == &KosDynamicRefType))

#define KosConstantRef_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == &KosConstantRefType))

#define KosMemoizedRef_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == &KosMemoizedRefType))

#define KosParamRef_CheckExact(obj)                     \
  ((obj) && ((obj)->ob_type == &KosParamRefType))


/* === Unit lifecycle === */


int koskari_ref_init(PyObject *mod);
int koskari_ref_clear(PyObject *mod);


#endif


// The end.
