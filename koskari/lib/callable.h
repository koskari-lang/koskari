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


#ifndef KOSKARI_CALLABLE_H
#define KOSKARI_CALLABLE_H


#include <Python.h>


struct KosContext;
struct KosEvent;


typedef int (*KosCallHandler)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    int is_resume,
    PyObject *arg1,
    PyObject *arg2);


typedef int (*KosInstallFunc)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,   /* may be NULL */
    int is_tail);


typedef struct {
  PyTypeObject type;
  KosCallHandler tp_kos_call;
  KosCallHandler tp_kos_tail_call;
  KosInstallFunc tp_kos_install;   /* may be NULL */
} KosCallableTypeObject;


extern KosCallableTypeObject KosCallableType;


#define KOS_CALLABLE_PYTYPE(typeobj) (&((typeobj).type))

#define KosCallable_Check(obj)                                           \
  ((obj) && PyType_IsSubtype((obj)->ob_type, KOS_CALLABLE_PYTYPE(KosCallableType)))

#define KosCallableType_CALL(typ)      (((KosCallableTypeObject *) (typ))->tp_kos_call)
#define KosCallableType_TAIL_CALL(typ) (((KosCallableTypeObject *) (typ))->tp_kos_tail_call)
#define KosCallableType_INSTALL(typ)   (((KosCallableTypeObject *) (typ))->tp_kos_install)


int kos_callable_call(struct KosContext *ctx, struct KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);
int kos_callable_tail_call(struct KosContext *ctx, struct KosEvent *out_ev,
                           PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);

int koskari_callable_init(PyObject *mod);
int koskari_callable_clear(PyObject *mod);


#endif


// The end.
