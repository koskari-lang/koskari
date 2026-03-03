/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   koskari/lib/sync.c

   Async-to-sync primitives. Imports koskari._sync and exposes
   run_sync and iter_sync for C callers and the module dict.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"


static PyObject *as_coroutine_func = NULL;
static PyObject *run_sync_func = NULL;
static PyObject *iter_sync_func = NULL;


PyObject *Koskari_RunSync(PyObject *awaitable) {

  if (! run_sync_func) {
    PyErr_SetString(PyExc_RuntimeError,
                    "run_sync not available (koskari.lib init failed?)");
    return NULL;
  }
  return PyObject_CallOneArg(run_sync_func, awaitable);
}


PyObject *Koskari_IterSync(PyObject *async_iterable) {

  if (! iter_sync_func) {
    PyErr_SetString(PyExc_RuntimeError,
                    "iter_sync not available (koskari.lib init failed?)");
    return NULL;
  }
  return PyObject_CallOneArg(iter_sync_func, async_iterable);
}


int koskari_sync_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  PyObject *sync_mod = NULL;

  if (! dict)
    return -1;

  /* Native test harness uses _koskari_test; no koskari package available */
  {
    PyObject *mod_name = PyModule_GetNameObject(mod);
    int is_harness = (mod_name &&
        PyUnicode_CompareWithASCIIString(mod_name, "_koskari_test") == 0);
    Py_XDECREF(mod_name);
    if (is_harness)
      return 0;
  }

  sync_mod = PyImport_ImportModule("koskari._sync");
  if (! sync_mod)
    return -1;

  as_coroutine_func = PyObject_GetAttrString(sync_mod, "as_coroutine");
  run_sync_func = PyObject_GetAttrString(sync_mod, "run_sync");
  iter_sync_func = PyObject_GetAttrString(sync_mod, "iter_sync");
  Py_DECREF(sync_mod);

  if (! as_coroutine_func || ! run_sync_func || ! iter_sync_func) {
    Py_XDECREF(as_coroutine_func);
    Py_XDECREF(run_sync_func);
    Py_XDECREF(iter_sync_func);
    as_coroutine_func = NULL;
    run_sync_func = NULL;
    iter_sync_func = NULL;
    return -1;
  }

  if (PyDict_SetItemString(dict, "as_coroutine", as_coroutine_func))
    return -1;
  if (PyDict_SetItemString(dict, "run_sync", run_sync_func))
    return -1;
  if (PyDict_SetItemString(dict, "iter_sync", iter_sync_func))
    return -1;

  return 0;
}


int koskari_sync_clear(PyObject *mod) {

  Py_CLEAR(as_coroutine_func);
  Py_CLEAR(run_sync_func);
  Py_CLEAR(iter_sync_func);
  (void) mod;
  return 0;
}


// The end.
