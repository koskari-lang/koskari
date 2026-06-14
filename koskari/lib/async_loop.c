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
   koskari.lib async loop driver

   asyncio host without asyncio.Runner SIGINT handling.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GPL v.3
   ai-assistant: claude-4.6-sonnet-high via Cursor
*/


#include "lib.h"


static PyObject *asyncio_new_event_loop = NULL;
static PyObject *asyncio_set_event_loop = NULL;
static PyObject *asyncio_all_tasks = NULL;
static PyObject *asyncio_gather = NULL;

static PyObject *str_run_until_complete = NULL;
static PyObject *str_cancel = NULL;
static PyObject *str_shutdown_asyncgens = NULL;
static PyObject *str_shutdown_default_executor = NULL;
static PyObject *str_close = NULL;


#define KOS_ASYNCIO_NEW_EVENT_LOOP()                                    \
  PyObject_CallNoArgs(asyncio_new_event_loop)


#define KOS_ASYNCIO_SET_EVENT_LOOP(loop)                                \
  PyObject_CallFunctionObjArgs(asyncio_set_event_loop, (loop), NULL)


#define KOS_ASYNCIO_ALL_TASKS(loop)                                     \
  PyObject_CallFunctionObjArgs(asyncio_all_tasks, (loop), NULL)


#define KOS_ASYNCIO_GATHER(args, kwargs)                               \
  PyObject_Call(asyncio_gather, (args), (kwargs))


#define KOS_LOOP_RUN_UNTIL_COMPLETE(loop, awaitable)                    \
  PyObject_CallMethodOneArg((loop), str_run_until_complete, (awaitable))


#define KOS_LOOP_SHUTDOWN_ASYNCGENS(loop)                               \
  PyObject_CallMethodNoArgs((loop), str_shutdown_asyncgens)


#define KOS_LOOP_SHUTDOWN_DEFAULT_EXECUTOR(loop)                        \
  PyObject_CallMethodNoArgs((loop), str_shutdown_default_executor)


#define KOS_LOOP_CLOSE(loop)                                            \
  PyObject_CallMethodNoArgs((loop), str_close)


#define KOS_TASK_CANCEL(task)                                           \
  PyObject_CallMethodNoArgs((task), str_cancel)


static PyObject *kos_asyncio_get_callable(PyObject *mod, const char *name) {

  PyObject *fn = PyObject_GetAttrString(mod, name);
  if (! fn)
    return NULL;

  if (! PyCallable_Check(fn)) {
    Py_DECREF(fn);
    PyErr_Format(PyExc_TypeError, "asyncio.%s is not callable", name);
    return NULL;
  }

  return fn;
}


static int kos_loop_run_until_complete(PyObject *loop, PyObject *awaitable) {

  PyObject *res = KOS_LOOP_RUN_UNTIL_COMPLETE(loop, awaitable);
  if (! res)
    return -1;

  Py_DECREF(res);
  return 0;
}


static int kos_asyncio_cancel_all_tasks(PyObject *loop) {

  PyObject *all_tasks = KOS_ASYNCIO_ALL_TASKS(loop);
  if (! all_tasks)
    return -1;

  if (PyObject_Length(all_tasks) == 0) {
    Py_DECREF(all_tasks);
    return 0;
  }

  PyObject *iterator = PyObject_GetIter(all_tasks);
  Py_DECREF(all_tasks);
  if (! iterator)
    return -1;

  PyObject *task = NULL;
  while ((task = PyIter_Next(iterator))) {
    PyObject *cancel_res = KOS_TASK_CANCEL(task);
    Py_DECREF(task);
    if (! cancel_res) {
      Py_DECREF(iterator);
      return -1;
    }
    Py_DECREF(cancel_res);
  }

  if (PyErr_Occurred()) {
    Py_DECREF(iterator);
    return -1;
  }

  Py_DECREF(iterator);
  return 0;
}


static int kos_asyncio_gather_all_tasks(PyObject *loop) {

  PyObject *all_tasks = KOS_ASYNCIO_ALL_TASKS(loop);
  if (! all_tasks)
    return -1;

  Py_ssize_t count = PyObject_Length(all_tasks);
  if (count < 0) {
    Py_DECREF(all_tasks);
    return -1;
  }

  if (count == 0) {
    Py_DECREF(all_tasks);
    return 0;
  }

  PyObject *task_tuple = PySequence_Tuple(all_tasks);
  Py_DECREF(all_tasks);
  if (! task_tuple)
    return -1;

  PyObject *kwargs = PyDict_New();
  if (! kwargs) {
    Py_DECREF(task_tuple);
    return -1;
  }

  if (PyDict_SetItemString(kwargs, "return_exceptions", Py_True) < 0) {
    Py_DECREF(kwargs);
    Py_DECREF(task_tuple);
    return -1;
  }

  PyObject *gathered = KOS_ASYNCIO_GATHER(task_tuple, kwargs);
  Py_DECREF(kwargs);
  Py_DECREF(task_tuple);
  if (! gathered)
    return -1;

  if (kos_loop_run_until_complete(loop, gathered) < 0) {
    Py_DECREF(gathered);
    return -1;
  }

  Py_DECREF(gathered);
  return 0;
}


static int kos_asyncio_shutdown_loop(PyObject *loop) {

  if (kos_asyncio_cancel_all_tasks(loop) < 0)
    return -1;

  if (kos_asyncio_gather_all_tasks(loop) < 0)
    return -1;

  PyObject *shutdown_asyncgens = KOS_LOOP_SHUTDOWN_ASYNCGENS(loop);
  if (! shutdown_asyncgens)
    return -1;

  if (kos_loop_run_until_complete(loop, shutdown_asyncgens) < 0) {
    Py_DECREF(shutdown_asyncgens);
    return -1;
  }
  Py_DECREF(shutdown_asyncgens);

  PyObject *shutdown_executor = KOS_LOOP_SHUTDOWN_DEFAULT_EXECUTOR(loop);
  if (! shutdown_executor)
    return -1;

  if (kos_loop_run_until_complete(loop, shutdown_executor) < 0) {
    Py_DECREF(shutdown_executor);
    return -1;
  }
  Py_DECREF(shutdown_executor);

  PyObject *clear_loop = KOS_ASYNCIO_SET_EVENT_LOOP(Py_None);
  if (! clear_loop)
    return -1;
  Py_DECREF(clear_loop);

  PyObject *close_res = KOS_LOOP_CLOSE(loop);
  if (! close_res)
    return -1;
  Py_DECREF(close_res);

  return 0;
}


/* === unit methods === */


PyObject *kos_drive_async_loop(PyObject *coro) {

  PyObject *result = NULL;
  PyObject *loop = NULL;

  if (! coro) {
    PyErr_SetString(PyExc_TypeError, "drive_async_loop() requires a coroutine");
    return NULL;
  }

  if (! asyncio_new_event_loop) {
    PyErr_SetString(PyExc_RuntimeError,
                    "asyncio loop driver is not initialized");
    return NULL;
  }

  loop = KOS_ASYNCIO_NEW_EVENT_LOOP();
  if (! loop)
    return NULL;

  PyObject *set_loop = KOS_ASYNCIO_SET_EVENT_LOOP(loop);
  if (! set_loop)
    goto fail;

  Py_DECREF(set_loop);

  result = KOS_LOOP_RUN_UNTIL_COMPLETE(loop, coro);
  if (! result)
    goto shutdown;

  if (kos_asyncio_shutdown_loop(loop) < 0) {
    Py_DECREF(result);
    result = NULL;
    goto fail;
  }

  Py_DECREF(loop);
  return result;

 shutdown:
  {
    PyObject *exc_type = NULL;
    PyObject *exc_val = NULL;
    PyObject *exc_tb = NULL;

    PyErr_Fetch(&exc_type, &exc_val, &exc_tb);
    if (kos_asyncio_shutdown_loop(loop) < 0)
      PyErr_Clear();
    PyErr_Restore(exc_type, exc_val, exc_tb);
  }

 fail:
  Py_XDECREF(loop);
  return result;
}


static PyObject *m_drive_async_loop(PyObject *mod, PyObject *coro) {

  (void) mod;

  if (! coro) {
    PyErr_SetString(PyExc_TypeError,
                    "drive_async_loop() requires a coroutine");
    return NULL;
  }

  return kos_drive_async_loop(coro);
}


static PyMethodDef async_loop_methods[] = {
  { "drive_async_loop", (PyCFunction) m_drive_async_loop, METH_O,
    "drive_async_loop(coro) -> result\n"
    "Run coroutine without asyncio.Runner SIGINT handling" },
  { NULL, },
};


/* === unit lifecycle === */


int koskari_async_loop_init(PyObject *mod) {

  PyObject *asyncio = NULL;

  if (! mod)
    return -1;

  STR_CONST(str_run_until_complete, "run_until_complete");
  STR_CONST(str_cancel, "cancel");
  STR_CONST(str_shutdown_asyncgens, "shutdown_asyncgens");
  STR_CONST(str_shutdown_default_executor, "shutdown_default_executor");
  STR_CONST(str_close, "close");

  asyncio = PyImport_ImportModule("asyncio");
  if (! asyncio)
    return -1;

  asyncio_new_event_loop = kos_asyncio_get_callable(asyncio, "new_event_loop");
  asyncio_set_event_loop = kos_asyncio_get_callable(asyncio, "set_event_loop");
  asyncio_all_tasks = kos_asyncio_get_callable(asyncio, "all_tasks");
  asyncio_gather = kos_asyncio_get_callable(asyncio, "gather");

  Py_DECREF(asyncio);

  if (! asyncio_new_event_loop ||
      ! asyncio_set_event_loop ||
      ! asyncio_all_tasks ||
      ! asyncio_gather) {
    koskari_async_loop_clear(mod);
    return -1;
  }

  for (PyMethodDef *mdef = async_loop_methods; mdef->ml_name; mdef++) {
    PyObject *func = PyCFunction_New(mdef, mod);
    if (! func)
      return -1;

    int res = PyObject_SetAttrString(mod, mdef->ml_name, func);
    Py_DECREF(func);

    if (res < 0)
      return -1;
  }

  return 0;
}


int koskari_async_loop_clear(PyObject *mod) {

  (void) mod;

  Py_CLEAR(asyncio_new_event_loop);
  Py_CLEAR(asyncio_set_event_loop);
  Py_CLEAR(asyncio_all_tasks);
  Py_CLEAR(asyncio_gather);
  Py_CLEAR(str_run_until_complete);
  Py_CLEAR(str_cancel);
  Py_CLEAR(str_shutdown_asyncgens);
  Py_CLEAR(str_shutdown_default_executor);
  Py_CLEAR(str_close);

  return 0;
}


// The end.
