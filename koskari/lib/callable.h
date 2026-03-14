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


/**
   Install functions receive borrowed references. callable, args_tuple,
   kwds_dict (or args, kwnames for vector) must not be DECREF'd by the
   implementation.

   tail_call: 1 if this is a tail call; 0 otherwise. Most implementations
   ignore this; frame behavior is handled by the caller (KosContext_Call*).
*/
typedef int (*KosInstallFunc)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call);


typedef int (*KosVectorInstallFunc)(
    struct KosContext *ctx,
    struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   Optional per-instance wants_frame. If non-NULL, called with the callable
   instance; return 1 if the call needs a frame (push or reset), 0 if not.
   Overrides tp_kos_wants_frame when present.
*/
typedef int (*KosCallableWantsFrameFunc)(PyObject *callable);


typedef struct {
  PyTypeObject type;
  KosInstallFunc tp_kos_install;        /* tuple+dict; may be NULL */
  KosVectorInstallFunc tp_kos_vinstall; /* vectorcall; may be NULL */
  int tp_kos_wants_frame;               /* default when wants_frame_fn is NULL */
  KosCallableWantsFrameFunc tp_kos_wants_frame_fn;  /* if non-NULL, overrides */
} KosCallableTypeObject;


extern KosCallableTypeObject KosCallableType;


#define KOS_CALLABLE_PYTYPE(typeobj) (&((typeobj).type))

#define KosCallable_Check(obj)                                           \
  ((obj) && PyType_IsSubtype((obj)->ob_type, KOS_CALLABLE_PYTYPE(KosCallableType)))

#define KosCallableType_INSTALL(typ)        (((KosCallableTypeObject *) (typ))->tp_kos_install)
#define KosCallableType_VINSTALL(typ)       (((KosCallableTypeObject *) (typ))->tp_kos_vinstall)
#define KosCallableType_WANTS_FRAME(typ)    (((KosCallableTypeObject *) (typ))->tp_kos_wants_frame)
#define KosCallableType_WANTS_FRAME_FN(typ) (((KosCallableTypeObject *) (typ))->tp_kos_wants_frame_fn)


/* === KosCallable_* : KosCallable-only invocation (no type check, no frame prep) === */

/**
   KosCallable_Call: invoke a KosCallable with vectorcall-style args.

   callable MUST be a KosCallable; no type check is performed. Caller is
   responsible for frame/stack state; this function only dispatches to the
   install slot. Prefers tp_kos_vinstall when present; otherwise builds
   tuple+dict and uses tp_kos_install.

   callable, args, kwnames are borrowed. Returns 0 continue, 1 suspend, -1 error.
*/
int KosCallable_Call(struct KosContext *ctx, struct KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call);

/**
   KosCallable_CallArgs: invoke a KosCallable with tuple+dict args.

   callable MUST be a KosCallable; no type check is performed. Caller is
   responsible for frame/stack state; this function only dispatches to
   tp_kos_install.

   callable, args_tuple, kwds_dict are borrowed. Returns 0 continue, 1 suspend, -1 error.
*/
int KosCallable_CallArgs(struct KosContext *ctx, struct KosEvent *out_ev,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call);

/**
   KosCallable_CallNoArgs: invoke a KosCallable with no arguments.

   callable MUST be a KosCallable; no type check is performed. Caller is
   responsible for frame/stack state; this function only dispatches to
   tp_kos_install with NULL args_tuple and NULL kwds_dict.

   callable is borrowed. Returns 0 continue, 1 suspend, -1 error.
*/
int KosCallable_CallNoArgs(struct KosContext *ctx, struct KosEvent *out_ev,
    PyObject *callable, int tail_call);


int koskari_callable_init(PyObject *mod);
int koskari_callable_clear(PyObject *mod);


#endif


// The end.
