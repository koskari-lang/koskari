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


typedef int (*KosInstallFunc)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict);   /* may be NULL */


typedef int (*KosVectorInstallFunc)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames);     /* may be NULL */


typedef struct {
  PyTypeObject type;
  KosInstallFunc tp_kos_install;            /* non-tail; may be NULL */
  KosInstallFunc tp_kos_tail_install;       /* tail; may be NULL; NULL => no TCO */
  KosVectorInstallFunc tp_kos_vinstall;     /* vectorcall non-tail; may be NULL */
  KosVectorInstallFunc tp_kos_tail_vinstall; /* vectorcall tail; may be NULL */
} KosCallableTypeObject;


extern KosCallableTypeObject KosCallableType;


#define KOS_CALLABLE_PYTYPE(typeobj) (&((typeobj).type))

#define KosCallable_Check(obj)                                           \
  ((obj) && PyType_IsSubtype((obj)->ob_type, KOS_CALLABLE_PYTYPE(KosCallableType)))

#define KosCallableType_INSTALL(typ)            (((KosCallableTypeObject *) (typ))->tp_kos_install)
#define KosCallableType_TAIL_INSTALL(typ)       (((KosCallableTypeObject *) (typ))->tp_kos_tail_install)
#define KosCallableType_VINSTALL(typ)           (((KosCallableTypeObject *) (typ))->tp_kos_vinstall)
#define KosCallableType_TAIL_VINSTALL(typ)      (((KosCallableTypeObject *) (typ))->tp_kos_tail_vinstall)


int kos_callable_call(struct KosContext *ctx, struct KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);
int kos_callable_tail_call(struct KosContext *ctx, struct KosEvent *out_ev,
                           PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);

int kos_callable_vcall(struct KosContext *ctx, struct KosEvent *out_ev,
                       PyObject *callable,
                       PyObject *const *args, Py_ssize_t nargs,
                       PyObject *kwnames);
/* Tail vcall: KosCallable path takes stack ownership (no DropNoErr by caller). */
int kos_callable_tail_vcall(struct KosContext *ctx, struct KosEvent *out_ev,
                            PyObject *callable,
                            PyObject *const *args, Py_ssize_t nargs,
                            PyObject *kwnames);

int koskari_callable_init(PyObject *mod);
int koskari_callable_clear(PyObject *mod);


#endif


// The end.
