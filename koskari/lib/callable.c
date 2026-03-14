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


#include "lib.h"


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)


/* === Helpers === */


static PyObject *vcall_build_args_tuple(PyObject *const *args, Py_ssize_t nargs) {
  PyObject *tup = PyTuple_New(nargs);
  if (! tup)
    return NULL;
  for (Py_ssize_t i = 0; i < nargs; i++) {
    Py_INCREF(args[i]);
    PyTuple_SET_ITEM(tup, i, args[i]);
  }
  return tup;
}


static PyObject *vcall_build_kwds_dict(
    PyObject *const *kw_vals,
    PyObject *kwnames) {

  Py_ssize_t nkw = PyTuple_GET_SIZE(kwnames);
  PyObject *kwds = PyDict_New();
  if (! kwds)
    return NULL;
  for (Py_ssize_t i = 0; i < nkw; i++) {
    if (PyDict_SetItem(kwds, PyTuple_GET_ITEM(kwnames, i), kw_vals[i]) < 0) {
      Py_DECREF(kwds);
      return NULL;
    }
  }
  return kwds;
}


/* === Frame prep helpers (context-owned) === */


static int get_effective_wants_frame(PyObject *callable, PyTypeObject *typ) {
  KosCallableTypeObject *ctyp = (KosCallableTypeObject *) typ;
  if (ctyp->tp_kos_wants_frame_fn)
    return ctyp->tp_kos_wants_frame_fn(callable);
  return ctyp->tp_kos_wants_frame;
}


static int do_frame_prep(KosContext *ctx, PyObject *callable, PyTypeObject *typ,
                         int tail_call) {

  int wants = get_effective_wants_frame(callable, typ);
  KosDynFrame *frame = CURRENT_FRAME(ctx);
  KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);

  if (wants && tail_call) {
    kos_dynframe_reset_for_tail(frame);
    return 0;
  }
  if (wants && ! tail_call) {
    if (kos_dyn_push_frame_empty(ctx) == NULL)
      return -1;
    return 0;
  }
  if (! wants && tail_call && parent) {
    return kos_dyn_pop_frame(ctx);
  }
  return 0;
}


/* === KosCallable_* : KosCallable-only (no type check, no frame prep) === */


int KosCallable_Call(KosContext *ctx, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  PyTypeObject *typ = Py_TYPE(callable);
  KosVectorInstallFunc vfn = KosCallableType_VINSTALL(typ);
  if (vfn)
    return vfn(ctx, out_ev, callable, args, nargs, kwnames, tail_call);

  KosInstallFunc fn = KosCallableType_INSTALL(typ);
  if (! fn)
    return -1;

  PyObject *tup = (nargs > 0) ? vcall_build_args_tuple(args, nargs) : NULL;
  if (nargs > 0 && ! tup)
    return -1;
  PyObject *kwds = NULL;
  if (kwnames) {
    kwds = vcall_build_kwds_dict(args + nargs, kwnames);
    if (! kwds) {
      Py_XDECREF(tup);
      return -1;
    }
  }
  int r = fn(ctx, out_ev, callable, tup, kwds, tail_call);
  Py_XDECREF(tup);
  Py_XDECREF(kwds);
  return r;
}


int KosCallable_CallArgs(KosContext *ctx, KosEvent *out_ev,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
  if (! fn)
    return -1;
  return fn(ctx, out_ev, callable, args_tuple, kwds_dict, tail_call);
}


int KosCallable_CallNoArgs(KosContext *ctx, KosEvent *out_ev,
    PyObject *callable, int tail_call) {

  KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
  if (! fn)
    return -1;
  return fn(ctx, out_ev, callable, NULL, NULL, tail_call);
}


/* === KosContext_Call* : generic entry points === */


int KosContext_Call(KosContext *ctx, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = CURRENT_FRAME(ctx);
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(ctx) < 0)
        return -1;
    }
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
    if (! result)
      return -1;
    int r = kos_push_or_await(ctx, out_ev, result);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;
    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(ctx, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_Call(ctx, out_ev, callable, args, nargs, kwnames,
                           tail_call);
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}


int KosContext_CallArgs(KosContext *ctx, KosEvent *out_ev,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = CURRENT_FRAME(ctx);
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(ctx) < 0)
        return -1;
    }
    PyObject *result = NULL;
    PyObject *empty_args = NULL;
    int has_no_args = (! args_tuple || PyTuple_GET_SIZE(args_tuple) == 0);

    if (has_no_args && ! kwds_dict) {
      result = PyObject_CallNoArgs(callable);
    } else {
      PyObject *call_args = args_tuple;
      if (! call_args) {
        empty_args = PyTuple_New(0);
        if (! empty_args)
          return -1;
        call_args = empty_args;
      }
      result = PyObject_Call(callable, call_args, kwds_dict ? kwds_dict : NULL);
    }
    Py_XDECREF(empty_args);
    if (! result)
      return -1;
    int r = kos_push_or_await(ctx, out_ev, result);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;
    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(ctx, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_CallArgs(ctx, out_ev, callable, args_tuple, kwds_dict,
                               tail_call);
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}


int KosContext_CallNoArgs(KosContext *ctx, KosEvent *out_ev,
    PyObject *callable, int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = CURRENT_FRAME(ctx);
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(ctx) < 0)
        return -1;
    }
    PyObject *result = PyObject_CallNoArgs(callable);
    if (! result)
      return -1;
    int r = kos_push_or_await(ctx, out_ev, result);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;
    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(ctx, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_CallNoArgs(ctx, out_ev, callable, tail_call);
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}


/* === KosCallableType === */


KosCallableTypeObject KosCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib._KosCallable",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Internal base type for native Koskari callables",
    .tp_new = NULL,
  },
  .tp_kos_install = NULL,
  .tp_kos_vinstall = NULL,
  .tp_kos_wants_frame = 0,
  .tp_kos_wants_frame_fn = NULL,
};


int koskari_callable_init(PyObject *mod) {
  (void) mod;
  return PyType_Ready(KOS_CALLABLE_PYTYPE(KosCallableType));
}


int koskari_callable_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
