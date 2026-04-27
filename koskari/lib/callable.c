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


/* === KosCallable_* : KosCallable-only (no type check, no frame prep) === */


int KosCallable_Call(KosEvalState *es, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  PyTypeObject *typ = Py_TYPE(callable);
  KosVectorInstallFunc vfn = KosCallableType_VINSTALL(typ);
  if (vfn)
    return vfn(es, out_ev, callable, args, nargs, kwnames, tail_call);

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
  int r = fn(es, out_ev, callable, tup, kwds, tail_call);
  Py_XDECREF(tup);
  Py_XDECREF(kwds);
  return r;
}


int KosCallable_CallArgs(KosEvalState *es, KosEvent *out_ev,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
  if (! fn)
    return -1;
  return fn(es, out_ev, callable, args_tuple, kwds_dict, tail_call);
}


int KosCallable_CallNoArgs(KosEvalState *es, KosEvent *out_ev,
    PyObject *callable, int tail_call) {

  KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
  if (! fn)
    return -1;
  return fn(es, out_ev, callable, NULL, NULL, tail_call);
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
