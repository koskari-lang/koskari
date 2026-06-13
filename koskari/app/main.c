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
   koskari native command entry point

   Embeds CPython, prepares the CLI coroutine in Python, and runs it
   through koskari.lib.drive_async_loop.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GPL v.3
   ai-assistant: claude-4.6-sonnet-high via Cursor
*/


#include <Python.h>
#include <stdio.h>
#include <stdlib.h>


static int kos_app_init_python(int argc, char **argv) {

  PyStatus status;
  PyConfig config;

  PyConfig_InitPythonConfig(&config);
  config.parse_argv = 0;
  status = PyConfig_SetBytesArgv(&config, argc, argv);
  if (PyStatus_Exception(status))
    goto fail;

  status = Py_InitializeFromConfig(&config);
  if (PyStatus_Exception(status))
    goto fail;

  PyConfig_Clear(&config);
  return 0;

 fail:
  PyConfig_Clear(&config);
  Py_ExitStatusException(status);
  return 1;
}


static PyObject *kos_app_import_module(const char *name) {

  PyObject *mod = PyImport_ImportModule(name);
  if (! mod) {
    fprintf(stderr, "failed to import %s\n", name);
    PyErr_Print();
  }
  return mod;
}


static int kos_app_prepare_cli(int *exit_code_out, PyObject **coro_out) {

  PyObject *cli_mod = NULL;
  PyObject *prepare_fn = NULL;
  PyObject *prepare_res = NULL;
  PyObject *sys_mod = NULL;
  PyObject *sys_argv = NULL;
  int exit_code = 1;

  cli_mod = kos_app_import_module("koskari.cli");
  if (! cli_mod)
    goto done;

  prepare_fn = PyObject_GetAttrString(cli_mod, "prepare_cli_async");
  if (! prepare_fn)
    goto done;

  sys_mod = PyImport_ImportModule("sys");
  if (! sys_mod)
    goto done;

  sys_argv = PyObject_GetAttrString(sys_mod, "argv");
  if (! sys_argv)
    goto done;

  prepare_res = PyObject_CallFunctionObjArgs(prepare_fn, sys_argv, NULL);
  if (! prepare_res)
    goto done;

  if (! PyTuple_Check(prepare_res) || PyTuple_GET_SIZE(prepare_res) != 2) {
    PyErr_SetString(PyExc_RuntimeError,
                    "prepare_cli_async must return a 2-tuple");
    goto done;
  }

  PyObject *sync_code = PyTuple_GET_ITEM(prepare_res, 0);
  PyObject *coro = PyTuple_GET_ITEM(prepare_res, 1);

  if (sync_code != Py_None) {
    if (! PyLong_Check(sync_code)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "prepare_cli_async sync exit code must be int");
      goto done;
    }
    *exit_code_out = (int) PyLong_AsLong(sync_code);
    if (*exit_code_out == -1 && PyErr_Occurred())
      goto done;
    *coro_out = NULL;
    exit_code = 0;
    goto done;
  }

  if (coro == Py_None) {
    PyErr_SetString(PyExc_RuntimeError,
                    "prepare_cli_async returned no coroutine");
    goto done;
  }

  Py_INCREF(coro);
  *exit_code_out = 0;
  *coro_out = coro;
  exit_code = 0;

 done:
  if (exit_code != 0 && PyErr_Occurred())
    PyErr_Print();

  Py_XDECREF(prepare_res);
  Py_XDECREF(sys_argv);
  Py_XDECREF(sys_mod);
  Py_XDECREF(prepare_fn);
  Py_XDECREF(cli_mod);
  return exit_code;
}


static int kos_app_run_coro(PyObject *coro) {

  PyObject *lib_mod = NULL;
  PyObject *drive_fn = NULL;
  PyObject *result = NULL;
  int exit_code = 1;

  lib_mod = kos_app_import_module("koskari.lib");
  if (! lib_mod)
    goto done;

  drive_fn = PyObject_GetAttrString(lib_mod, "drive_async_loop");
  if (! drive_fn)
    goto done;

  result = PyObject_CallFunctionObjArgs(drive_fn, coro, NULL);
  if (! result) {
    if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
      PyErr_Clear();
      fprintf(stderr, "\n");
      exit_code = 130;
      goto done;
    }
    if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
      PyObject *typ = NULL;
      PyObject *val = NULL;
      PyObject *tb = NULL;

      PyErr_Fetch(&typ, &val, &tb);
      Py_XDECREF(typ);
      Py_XDECREF(tb);
      if (val && val != Py_None) {
        PyObject *code_obj = val;
        if (PyTuple_Check(val) && PyTuple_GET_SIZE(val) == 1)
          code_obj = PyTuple_GET_ITEM(val, 0);
        if (PyLong_Check(code_obj))
          exit_code = (int) PyLong_AsLong(code_obj);
      }
      Py_XDECREF(val);
      goto done;
    }
    PyErr_Print();
    goto done;
  }

  exit_code = 0;

 done:
  Py_XDECREF(result);
  Py_XDECREF(drive_fn);
  Py_XDECREF(lib_mod);
  return exit_code;
}


int main(int argc, char **argv) {

  int exit_code = 1;
  int sync_exit = 0;
  PyObject *coro = NULL;

  if (kos_app_init_python(argc, argv) != 0)
    return 1;

  if (! kos_app_import_module("koskari"))
    goto finalize;

  if (kos_app_prepare_cli(&sync_exit, &coro) != 0)
    goto finalize;

  if (! coro) {
    exit_code = sync_exit;
    goto finalize;
  }

  exit_code = kos_app_run_coro(coro);

 finalize:
  Py_XDECREF(coro);
  if (Py_FinalizeEx() < 0)
    return 120;

  return exit_code;
}


// The end.
