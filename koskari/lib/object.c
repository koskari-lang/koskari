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
   koskari/lib/object.c

   KosType native metaclass: awaitable tp_call, C.new sync adapter,
   and KosCallable install slot for (C ...) in Koskari bytecode.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)  KosDynFrame_STACK(CURRENT_FRAME(ctx))


/* === kos_construct === */


PyObject *kos_construct(PyObject *cls, PyObject *args, PyObject *kwargs,
                       int mode) {

  PyTypeObject *type = (PyTypeObject *) cls;
  PyObject *instance = type->tp_alloc(type, 0);
  if (! instance)
    return NULL;

  PyObject *init = NULL;
  if (type->tp_dict)
    init = PyDict_GetItemString(type->tp_dict, "__init__");

  if (! init) {
    if (mode == KOS_CONSTRUCT_MODE_SYNC)
      return instance;
    /* MODE_ASYNC: wrap in awaitable that yields immediately */
    return kos_construct_awaitable_new(instance, NULL);
  }

  PyObject *init_args = PyTuple_Pack(1, instance);
  if (! init_args) {
    Py_DECREF(instance);
    return NULL;
  }
  PyObject *full_args = PySequence_Concat(init_args, args ? args : PyTuple_New(0));
  Py_DECREF(init_args);
  if (! full_args) {
    Py_DECREF(instance);
    return NULL;
  }

  PyObject *result = PyObject_Call(init, full_args, kwargs);
  Py_DECREF(full_args);
  if (! result) {
    Py_DECREF(instance);
    return NULL;
  }

  if (mode == KOS_CONSTRUCT_MODE_SYNC) {
    PyTypeObject *res_type = Py_TYPE(result);
    if (res_type->tp_as_async && res_type->tp_as_async->am_await) {
      PyObject *done = Koskari_RunSync(result);
      Py_DECREF(result);
      if (! done) {
        Py_DECREF(instance);
        return NULL;
      }
      Py_DECREF(done);
    }
    else {
      Py_DECREF(result);
    }
    return instance;
  }

  /* MODE_ASYNC: wrap (instance, result) in awaitable */
  PyObject *awaitable = kos_construct_awaitable_new(instance, result);
  Py_DECREF(result);
  if (! awaitable) {
    Py_DECREF(instance);
    return NULL;
  }
  return awaitable;
}


/* === KosTypeConstructAwaitable === */


typedef struct {
  PyObject_HEAD
  PyObject *instance;
  PyObject *init_result;
} KosTypeConstructAwaitable;


static void construct_awaitable_dealloc(PyObject *self) {
  Py_CLEAR(((KosTypeConstructAwaitable *) self)->instance);
  Py_CLEAR(((KosTypeConstructAwaitable *) self)->init_result);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *construct_awaitable_iter(PyObject *self) {
  Py_INCREF(self);
  return self;
}


static PyObject *construct_awaitable_iternext(PyObject *self) {
  KosTypeConstructAwaitable *a = (KosTypeConstructAwaitable *) self;
  PyObject *instance = a->instance;
  PyObject *init_result = a->init_result;

  if (! instance)
    return NULL;

  if (! init_result) {
    a->instance = NULL;
    Py_INCREF(instance);
    PyErr_SetObject(PyExc_StopIteration, instance);
    return NULL;
  }

  PyTypeObject *res_type = Py_TYPE(init_result);
  if (! res_type->tp_as_async || ! res_type->tp_as_async->am_await) {
    a->instance = NULL;
    Py_CLEAR(a->init_result);
    Py_INCREF(instance);
    PyErr_SetObject(PyExc_StopIteration, instance);
    return NULL;
  }

  PyObject *iter = res_type->tp_as_async->am_await(init_result);
  if (! iter)
    return NULL;
  PyObject *next_val = PyIter_Next(iter);
  Py_DECREF(iter);
  if (next_val)
    return next_val;
  if (PyErr_Occurred())
    return NULL;
  /* init coroutine exhausted; return instance via StopIteration */
  a->instance = NULL;
  Py_CLEAR(a->init_result);
  Py_INCREF(instance);
  PyErr_SetObject(PyExc_StopIteration, instance);
  return NULL;
}


static PyObject *construct_awaitable_am_await(PyObject *self) {
  Py_INCREF(self);
  return self;
}


static PyAsyncMethods construct_awaitable_async = {
  .am_await = construct_awaitable_am_await,
};


static PyTypeObject KosTypeConstructAwaitableType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib._KosTypeConstructAwaitable",
  .tp_basicsize = sizeof(KosTypeConstructAwaitable),
  .tp_dealloc = construct_awaitable_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_iter = construct_awaitable_iter,
  .tp_iternext = construct_awaitable_iternext,
  .tp_as_async = &construct_awaitable_async,
};


PyObject *kos_construct_awaitable_new(PyObject *instance,
                                       PyObject *init_result) {

  if (PyType_Ready(&KosTypeConstructAwaitableType) < 0)
    return NULL;
  KosTypeConstructAwaitable *a = (KosTypeConstructAwaitable *)
      KosTypeConstructAwaitableType.tp_alloc(&KosTypeConstructAwaitableType, 0);
  if (! a)
    return NULL;
  Py_INCREF(instance);
  a->instance = instance;
  a->init_result = init_result ? init_result : NULL;
  if (init_result)
    Py_INCREF(init_result);
  return (PyObject *) a;
}


/* === kostype_tp_call === */


static PyObject *kostype_tp_call(PyObject *cls, PyObject *args,
                                 PyObject *kwargs) {

  /* 3-arg form (name, bases, namespace) is class creation; delegate
     to type's tp_call. Instance construction uses kos_construct. */
  if (args && PyTuple_GET_SIZE(args) == 3
      && PyUnicode_Check(PyTuple_GET_ITEM(args, 0))
      && PyTuple_Check(PyTuple_GET_ITEM(args, 1))
      && PyDict_Check(PyTuple_GET_ITEM(args, 2))) {
    return PyType_Type.tp_call(cls, args, kwargs);
  }
  return kos_construct(cls, args, kwargs, KOS_CONSTRUCT_MODE_ASYNC);
}


/* === C.new classmethod === */


static PyObject *kostype_new_classmethod(PyObject *cls, PyObject *args,
                                         PyObject *kwargs) {

  return kos_construct(cls, args, kwargs, KOS_CONSTRUCT_MODE_SYNC);
}


/* === kostype_tp_new === */


static PyMethodDef kostype_new_method_def = {
  .ml_name = "new",
  .ml_meth = (PyCFunction) kostype_new_classmethod,
  .ml_flags = METH_VARARGS | METH_KEYWORDS,
  .ml_doc = "Synchronous constructor. Use when __init__ is async.",
};


static PyObject *kostype_tp_new(PyTypeObject *metaclass, PyObject *args,
                                PyObject *kwds) {

  PyObject *cls = PyType_Type.tp_new(metaclass, args, kwds);
  if (! cls)
    return NULL;

  PyObject *new_cm = PyDescr_NewClassMethod(
      (PyTypeObject *) cls, &kostype_new_method_def);
  if (new_cm) {
    if (PyDict_SetItemString(((PyTypeObject *) cls)->tp_dict, "new", new_cm) < 0)
      Py_DECREF(new_cm);
    Py_DECREF(new_cm);
  }
  return cls;
}


/* === kostype_init_done_handler === */


typedef int (*KosNativeHandlerFn)(void *, void *, int, PyObject *);


static int kostype_init_done_handler(KosContext *ctx, KosEvent *out_ev,
                                     int is_resume, PyObject *delivered_value) {

  (void) is_resume;
  (void) delivered_value;
  KosDynFrame *frame = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame);
  PyObject *instance = KosStackBlock_PopRef(stack);
  if (! instance)
    return -1;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
  KosDynFrame *detached = NULL;
  if (kos_dyn_unwind_to_frame(ctx, frame, out_ev) < 0) {
    Py_DECREF(instance);
    return -1;
  }
  if (kos_dyn_detach_top_frame(ctx, &detached) < 0) {
    Py_DECREF(instance);
    return -1;
  }
  if (detached)
    kos_dynframe_free(detached);
  return kos_push_or_await(ctx, out_ev, instance);
}


/* === kostype_install === */


int kostype_install(KosContext *ctx, KosEvent *out_ev,
                    PyObject *callable, PyObject *args_tuple,
                    PyObject *kwds_dict) {

  if (! KosType_Check(callable)) {
    PyErr_SetString(PyExc_TypeError, "kostype_install requires KosType");
    return -1;
  }

  PyTypeObject *cls = (PyTypeObject *) callable;
  PyObject *instance = cls->tp_alloc(cls, 0);
  if (! instance)
    return -1;

  PyObject *init = NULL;
  if (cls->tp_dict)
    init = PyDict_GetItemString(cls->tp_dict, "__init__");

  if (! init) {
    int r = kos_push_or_await(ctx, out_ev, instance);
    Py_DECREF(instance);
    return r;
  }

  if (KosCallable_Check(init)) {
    PyObject *init_args = PyTuple_Pack(1, instance);
    if (! init_args) {
      Py_DECREF(instance);
      return -1;
    }
    PyObject *full_args = PySequence_Concat(
        init_args, args_tuple ? args_tuple : PyTuple_New(0));
    Py_DECREF(init_args);
    if (! full_args) {
      Py_DECREF(instance);
      return -1;
    }
    KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
    if (! frame) {
      Py_DECREF(full_args);
      Py_DECREF(instance);
      return -1;
    }
    KosDynFrame_NATIVE_HANDLER(frame) = (KosNativeHandlerFn) kostype_init_done_handler;
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), instance) < 0) {
      Py_DECREF(instance);
      Py_DECREF(full_args);
      return -1;
    }
    Py_DECREF(instance);
    KosInstallFunc inner = KosCallableType_INSTALL(Py_TYPE(init));
    if (inner) {
      int r = inner(ctx, out_ev, init, full_args, kwds_dict);
      Py_DECREF(full_args);
      return r;
    }
    Py_DECREF(full_args);
    PyErr_SetString(PyExc_TypeError, "KosCallable __init__ has no install slot");
    return -1;
  }

  /* Plain Python callable */
  PyObject *init_args = PyTuple_Pack(1, instance);
  if (! init_args) {
    Py_DECREF(instance);
    return -1;
  }
  PyObject *full_args = PySequence_Concat(
      init_args, args_tuple ? args_tuple : PyTuple_New(0));
  Py_DECREF(init_args);
  if (! full_args) {
    Py_DECREF(instance);
    return -1;
  }
  PyObject *result = PyObject_Call(init, full_args, kwds_dict);
  Py_DECREF(full_args);
  if (! result) {
    Py_DECREF(instance);
    return -1;
  }
  PyTypeObject *res_type = Py_TYPE(result);
  if (res_type->tp_as_async && res_type->tp_as_async->am_await) {
    PyObject *aw = kos_construct_awaitable_new(instance, result);
    Py_DECREF(result);
    if (! aw) {
      Py_DECREF(instance);
      return -1;
    }
    return kos_push_or_await(ctx, out_ev, aw);
  }
  Py_DECREF(result);
  return kos_push_or_await(ctx, out_ev, instance);
}


/* === kos_get_default_metaclass === */


PyObject *kos_get_default_metaclass(void) {
  PyObject *meta = (PyObject *) KOS_CALLABLE_PYTYPE(KosTypeType);
  Py_INCREF(meta);
  return meta;
}


/* === KosTypeType === */


KosCallableTypeObject KosTypeType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib.KosType",
    .tp_basicsize = sizeof(PyHeapTypeObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Metaclass for Koskari classes. Instances are classes. "
              "Calling C(...) returns an awaitable yielding the instance.",
    .tp_new = kostype_tp_new,
    .tp_call = kostype_tp_call,
    .tp_base = &PyType_Type,
    .tp_bases = NULL,
  },
  .tp_kos_install = kostype_install,
  .tp_kos_tail_install = NULL,
  .tp_kos_vinstall = NULL,
  .tp_kos_tail_vinstall = NULL,
};


/* === Unit lifecycle === */


int koskari_object_init(PyObject *mod) {

  if (PyType_Ready(&KosTypeConstructAwaitableType) < 0)
    return -1;

  PyObject *bases = PyTuple_Pack(2,
      (PyObject *) &PyType_Type,
      (PyObject *) KOS_CALLABLE_PYTYPE(KosCallableType));
  if (! bases)
    return -1;
  PyTypeObject *kt = KOS_CALLABLE_PYTYPE(KosTypeType);
  kt->tp_base = &PyType_Type;
  kt->tp_bases = bases;
  if (PyType_Ready(kt) < 0) {
    Py_DECREF(bases);
    return -1;
  }
  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;
  if (PyDict_SetItemString(dict, "KosType",
                           (PyObject *) KOS_CALLABLE_PYTYPE(KosTypeType)) < 0)
    return -1;
  return 0;
}


int koskari_object_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
