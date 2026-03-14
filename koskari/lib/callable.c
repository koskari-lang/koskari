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


/* === Default Python call paths (non-KosCallable or no install slot) === */


static int call_default_python_call(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  PyObject *result = (! args_tuple || PyTuple_GET_SIZE(args_tuple) == 0)
      ? PyObject_CallNoArgs(callable)
      : PyObject_Call(callable, args_tuple, kwds_dict ? kwds_dict : NULL);
  if (! result)
    return -1;
  return kos_push_or_await(ctx, out_ev, result);
}


static int call_default_python_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  PyObject *result = PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
  if (! result)
    return -1;
  return kos_push_or_await(ctx, out_ev, result);
}


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


/* === kos_callable_* public API === */


int kos_callable_call(KosContext *ctx, KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict) {
  if (KosCallable_Check(callable)) {
    KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn)
      return fn(ctx, out_ev, callable, args_tuple, kwds_dict);
  }
  return call_default_python_call(ctx, out_ev, callable, args_tuple, kwds_dict);
}


int kos_callable_tail_call(KosContext *ctx, KosEvent *out_ev,
                           PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict) {
  if (KosCallable_Check(callable)) {
    KosInstallFunc fn = KosCallableType_TAIL_INSTALL(Py_TYPE(callable));
    if (fn)
      return fn(ctx, out_ev, callable, args_tuple, kwds_dict);
    fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn) {
      KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
      if (! frame)
        return -1;
      return fn(ctx, out_ev, callable, args_tuple, kwds_dict);
    }
  }
  return call_default_python_call(ctx, out_ev, callable, args_tuple, kwds_dict);
}


int kos_callable_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  if (KosCallable_Check(callable)) {
    KosVectorInstallFunc vfn = KosCallableType_VINSTALL(Py_TYPE(callable));
    if (vfn)
      return vfn(ctx, out_ev, callable, args, nargs, kwnames);

    KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn) {
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
      int r = fn(ctx, out_ev, callable, tup, kwds);
      Py_XDECREF(tup);
      Py_XDECREF(kwds);
      return r;
    }
  }

  return call_default_python_vcall(ctx, out_ev, callable, args, nargs, kwnames);
}


int kos_callable_tail_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  if (KosCallable_Check(callable)) {
    KosVectorInstallFunc vfn = KosCallableType_TAIL_VINSTALL(Py_TYPE(callable));
    if (vfn)
      return vfn(ctx, out_ev, callable, args, nargs, kwnames);

    vfn = KosCallableType_VINSTALL(Py_TYPE(callable));
    if (vfn) {
      KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
      if (! frame)
        return -1;
      return vfn(ctx, out_ev, callable, args, nargs, kwnames);
    }

    KosInstallFunc fn = KosCallableType_TAIL_INSTALL(Py_TYPE(callable));
    if (fn) {
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
      int r = fn(ctx, out_ev, callable, tup, kwds);
      Py_XDECREF(tup);
      Py_XDECREF(kwds);
      return r;
    }
    fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn) {
      KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
      if (! frame)
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
      int r = fn(ctx, out_ev, callable, tup, kwds);
      Py_XDECREF(tup);
      Py_XDECREF(kwds);
      return r;
    }
  }

  return call_default_python_vcall(ctx, out_ev, callable, args, nargs, kwnames);
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
  .tp_kos_tail_install = NULL,
  .tp_kos_vinstall = NULL,
  .tp_kos_tail_vinstall = NULL,
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
