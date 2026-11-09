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
   koskari.lib

   Native extension module for Koskari

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "debug.h"
#include "lib.h"
#include "tracing.h"


/* === module methods === */


static PyObject *lib_get_trace_report(PyObject *mod, PyObject *unused) {
  (void) mod;
  (void) unused;
  return kos_trace_report();
}


static PyObject *lib_clear_trace_counters(PyObject *mod, PyObject *unused) {
  (void) mod;
  (void) unused;
  kos_trace_clear();
  Py_RETURN_NONE;
}


static PyObject *lib_get_frame_depth_limit(PyObject *mod, PyObject *unused) {
  (void) mod;
  (void) unused;
  return PyLong_FromUnsignedLong(kos_frame_depth_limit);
}


static PyObject *lib_set_frame_depth_limit(PyObject *mod, PyObject *arg) {
  int overflow = 0;
  long n;

  (void) mod;

  if (! PyLong_Check(arg)) {
    PyErr_Format(PyExc_TypeError,
                 "frame depth limit must be an int, not %.200s",
                 Py_TYPE(arg)->tp_name);
    return NULL;
  }

  n = PyLong_AsLongAndOverflow(arg, &overflow);
  if (n == -1 && PyErr_Occurred())
    return NULL;

  if (overflow || n < 1 || n > KOS_FRAME_DEPTH_MAX) {
    PyErr_Format(PyExc_ValueError,
                 "frame depth limit must be between 1 and %u",
                 (unsigned) KOS_FRAME_DEPTH_MAX);
    return NULL;
  }

  kos_frame_depth_limit = (unsigned) n;
  Py_RETURN_NONE;
}


static PyMethodDef libmod_methods[] = {
  { "get_trace_report", (PyCFunction) lib_get_trace_report, METH_NOARGS,
    "Return a dict of compile-time tracing report (flags and per-feature data)" },
  { "clear_trace_counters", (PyCFunction) lib_clear_trace_counters, METH_NOARGS,
    "Reset all compile-time trace counters/timers for enabled build features "
    "(same as module init/clear)" },
  { "get_frame_depth_limit", (PyCFunction) lib_get_frame_depth_limit,
    METH_NOARGS,
    "Return the process-wide limit on Koskari frame depth (default 10000)" },
  { "set_frame_depth_limit", (PyCFunction) lib_set_frame_depth_limit,
    METH_O,
    "Set the process-wide limit on Koskari frame depth, from 1 up to "
    "65535; installing a frame past it raises RecursionError" },
  { NULL, },
};


/* === build flags === */


static int build_flag_set(PyObject *flags, const char *name, long value) {
  PyObject *v = PyLong_FromLong(value);
  int res;
  if (! v)
    return -1;
  res = PyDict_SetItemString(flags, name, v);
  Py_DECREF(v);
  return res;
}


/**
   Build koskari.lib.build_flags: a read-only mapping from the name of
   each build knob defined when this extension compiled to its integer
   value. The knob list is build_flags.incl. Returns a new reference.
*/
static PyObject *build_flags_new(void) {
  PyObject *flags = PyDict_New();
  PyObject *proxy;
  if (! flags)
    return NULL;

  /* (NAME + 0) reads a knob defined with no value as 0 */
#define FLAG(NAME)                                                      \
  if (build_flag_set(flags, #NAME, (long) (NAME + 0)) < 0) {           \
    Py_DECREF(flags);                                                   \
    return NULL;                                                        \
  }
#include "build_flags.incl"
#undef FLAG

  proxy = PyDictProxy_New(flags);
  Py_DECREF(flags);
  return proxy;
}


/* === module lifecycle === */


/* The statics have been cleared since the last init: set before the
   unit clears run, so a clear that failed partway is not repeated
   either, and reset by koskari_lib_init. */
static int lib_cleared = 0;


int koskari_lib_init(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *const dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  lib_cleared = 0;
  kos_trace_init();

  {
    PyObject *flags = build_flags_new();
    int res;
    if (! flags)
      return -1;
    res = PyDict_SetItemString(dict, "build_flags", flags);
    Py_DECREF(flags);
    if (res < 0)
      return -1;
  }

  /* compat interns the coroutine protocol names and the cr_await
     descriptor before coro and iter use them */
  if (koskari_arena_init(mod) ||
      koskari_atom_init(mod) ||
      koskari_bytecode_init(mod) ||
      koskari_evaluated_init(mod) ||
      koskari_callable_init(mod) ||
      koskari_compat_init(mod) ||
      koskari_demand_init(mod) ||
      koskari_object_init(mod) ||
      koskari_cont_init(mod) ||
      koskari_coro_init(mod) ||
      koskari_domain_init(mod) ||
      koskari_eval_init(mod) ||
      koskari_formals_init(mod) ||
      koskari_func_init(mod) ||
      koskari_iter_init(mod) ||
      koskari_iterate_init(mod) ||
      koskari_transform_init(mod) ||
      koskari_matcher_init(mod) ||
      koskari_namespace_init(mod) ||
      koskari_operator_init(mod) ||
      koskari_pair_init(mod) ||
      koskari_parse_init(mod) ||
      koskari_ref_init(mod) ||
      koskari_stack_init(mod) ||
      koskari_utils_init(mod) ||
      koskari_values_init(mod) ||
      koskari_defer_init(mod) ||
      koskari_async_loop_init(mod) ||
      koskari_property_init(mod) ||
      koskari_partial_init(mod))
    return -1;

  return 0;
}


/* A single-phase module has one set of statics for the process. A
   re-import after a collection or an import from a legacy
   subinterpreter runs PyInit_lib again over them, and the guarded unit
   inits leave what stands in place while filling the new module dict.
   No module slot clears the statics: the static types and their
   instances hold no reference to the module object, so they outlive
   it, and a finalizer that runs after the module's collection at
   interpreter shutdown still interns a symbol or renders a pair. The
   clear is for a host that fills a module of its own and tears it down
   itself, as the native test harness does. */
int koskari_lib_clear(PyObject *mod) {
  if (! mod)
    return -1;

  if (lib_cleared)
    return 0;
  lib_cleared = 1;

  PyObject *const dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* clear implementation units */
  if (koskari_transform_clear(mod))
    return -1;

  if (koskari_iterate_clear(mod))
    return -1;

  if (koskari_iter_clear(mod))
    return -1;

  if (koskari_matcher_clear(mod))
    return -1;

  if (koskari_demand_clear(mod))
    return -1;

  if (koskari_object_clear(mod))
    return -1;

  if (koskari_cont_clear(mod))
    return -1;

  if (koskari_coro_clear(mod))
    return -1;

  if (koskari_func_clear(mod))
    return -1;

  if (koskari_eval_clear(mod))
    return -1;

  if (koskari_domain_clear(mod))
    return -1;

  if (koskari_parse_clear(mod))
    return -1;

  if (koskari_utils_clear(mod))
    return -1;

  if (koskari_stack_clear(mod))
    return -1;

  if (koskari_operator_clear(mod))
    return -1;

  if (koskari_values_clear(mod))
    return -1;

  if (koskari_defer_clear(mod))
    return -1;

  if (koskari_async_loop_clear(mod))
    return -1;

  if (koskari_property_clear(mod))
    return -1;

  if (koskari_partial_clear(mod))
    return -1;

  if (koskari_formals_clear(mod))
    return -1;

  if (koskari_bytecode_clear(mod))
    return -1;

  if (koskari_namespace_clear(mod))
    return -1;

  if (koskari_ref_clear(mod))
    return -1;

  if (koskari_pair_clear(mod))
    return -1;

  if (koskari_callable_clear(mod))
    return -1;

  if (koskari_evaluated_clear(mod))
    return -1;

  if (koskari_atom_clear(mod))
    return -1;

  if (koskari_arena_clear(mod))
    return -1;

  kos_trace_clear();

  /* compat should be cleared last */
  if (koskari_compat_clear(mod))
    return -1;

  PyDict_Clear(dict);

  return 0;
}


/* === module definition === */


static struct PyModuleDef libmod = {
  .m_base = PyModuleDef_HEAD_INIT,
  .m_name = "koskari.lib",
  .m_doc = "Native extension module for Koskari",
  /* 0, not -1: the module keeps no per-module state, and its init may
     run again, which is what a re-import after a collection needs */
  .m_size = 0,
  .m_methods = libmod_methods,
  .m_slots = NULL,
  .m_traverse = NULL,
  .m_clear = NULL,
  .m_free = NULL,
};


PyMODINIT_FUNC PyInit_lib(void) {
  PyObject *mod = PyModule_Create(&libmod);
  if (! mod)
    return NULL;

  if (koskari_lib_init(mod)) {
    Py_DECREF(mod);
    return NULL;
  }

  return mod;
}


// The end.
