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
   koskari/lib/func.c

   Expression and Function native types. Assembled from Code and
   namespace/formals the same way as sibilant-ceval; calling either
   returns a Coroutine.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"


/* === KosExpression: callable (code, ns) -> coroutine on __call__ === */


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *ns;
} KosExpression;


static int expression_init(KosExpression *self, PyObject *args,
                            PyObject *kwargs) {
  PyObject *code = NULL;
  PyObject *ns = NULL;

  (void) kwargs;
  if (! PyArg_ParseTuple(args, "OO:Expression", &code, &ns))
    return -1;
  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "expected Code, got %s",
                 Py_TYPE(code)->tp_name);
    return -1;
  }
  if (! ns) {
    PyErr_SetString(PyExc_TypeError, "namespace is required");
    return -1;
  }
  Py_INCREF(code);
  Py_INCREF(ns);
  self->code = code;
  self->ns = ns;
  return 0;
}


static void expression_dealloc(PyObject *self) {
  KosExpression *ex = (KosExpression *) self;
  Py_CLEAR(ex->code);
  Py_CLEAR(ex->ns);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *expression_call(PyObject *self, PyObject *args,
                                 PyObject *kwargs) {
  KosExpression *ex = (KosExpression *) self;

  (void) kwargs;
  if (PyTuple_GET_SIZE(args) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression() takes no arguments");
    return NULL;
  }
  return kos_run_code_as_coroutine(ex->code, ex->ns);
}


static PyMemberDef expression_members[] = {
  { "code", T_OBJECT, offsetof(KosExpression, code), READONLY, NULL },
  { "ns", T_OBJECT, offsetof(KosExpression, ns), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


PyTypeObject KosExpressionType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Expression",
  .tp_basicsize = sizeof(KosExpression),
  .tp_dealloc = expression_dealloc,
  .tp_repr = (reprfunc) NULL,
  .tp_call = expression_call,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Callable(Code, namespace). Calling with () returns a Coroutine.",
  .tp_init = (initproc) expression_init,
  .tp_new = PyType_GenericNew,
  .tp_members = expression_members,
};


/* === KosFunction: callable (code, base_ns, formals_spec) or (code, formals, base_ns) -> coroutine on __call__ === */


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *base_ns;
  PyObject *formals_spec;   /* set when created via Function(code, base_ns, formals_spec) */
  PyObject *formals;        /* set when created via Code.function(formals, parent_ns) */
} KosFunction;


static int function_init(KosFunction *self, PyObject *args,
                         PyObject *kwargs) {
  PyObject *code = NULL;
  PyObject *base_ns = NULL;
  PyObject *formals_spec = NULL;

  (void) kwargs;
  if (! PyArg_ParseTuple(args, "OOO:Function", &code, &base_ns, &formals_spec))
    return -1;
  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "expected Code, got %s",
                 Py_TYPE(code)->tp_name);
    return -1;
  }
  if (! KosFormalsSpec_CheckExact(formals_spec)) {
    PyErr_Format(PyExc_TypeError, "expected FormalsSpec, got %s",
                 Py_TYPE(formals_spec)->tp_name);
    return -1;
  }
  if (! base_ns) {
    PyErr_SetString(PyExc_TypeError, "base namespace is required");
    return -1;
  }
  Py_INCREF(code);
  Py_INCREF(base_ns);
  Py_INCREF(formals_spec);
  self->code = code;
  self->base_ns = base_ns;
  self->formals_spec = formals_spec;
  self->formals = NULL;
  return 0;
}


static void function_dealloc(PyObject *self) {
  KosFunction *fn = (KosFunction *) self;
  Py_CLEAR(fn->code);
  Py_CLEAR(fn->base_ns);
  Py_CLEAR(fn->formals_spec);
  Py_CLEAR(fn->formals);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *function_call(PyObject *self, PyObject *args,
                               PyObject *kwargs) {
  KosFunction *fn = (KosFunction *) self;
  PyObject *formals = NULL;
  PyObject *nspec = NULL;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  if (! kwargs)
    kwargs = PyDict_New();
  else
    Py_INCREF(kwargs);

  if (fn->formals) {
    /* Assembled via Code.function(formals, parent_ns) */
    formals = fn->formals;
    Py_INCREF(formals);
    nspec = KosFormals_NamespaceSpec(formals, NULL);
    if (! nspec)
      goto fail;
    frame_ns = PyObject_CallMethod(nspec, "get_namespace", "O", fn->base_ns);
    Py_DECREF(nspec);
    nspec = NULL;
    if (! frame_ns)
      goto fail;
    if (KosFormals_Apply(formals, frame_ns, args, kwargs) < 0)
      goto fail;
    Py_DECREF(formals);
    formals = NULL;
  } else {
    /* Assembled via Function(code, base_ns, formals_spec) */
    formals = PyObject_CallObject(fn->formals_spec, NULL);
    if (! formals)
      goto fail;
    nspec = KosFormalsSpec_NamespaceSpec(fn->formals_spec, NULL);
    if (! nspec)
      goto fail;
    frame_ns = PyObject_CallMethod(nspec, "get_namespace", "O", fn->base_ns);
    Py_DECREF(nspec);
    nspec = NULL;
    if (! frame_ns)
      goto fail;
    if (KosFormals_Apply(formals, frame_ns, args, kwargs) < 0)
      goto fail;
    Py_DECREF(formals);
    formals = NULL;
  }

  Py_DECREF(kwargs);
  kwargs = NULL;
  coro = kos_run_code_as_coroutine(fn->code, frame_ns);
  Py_DECREF(frame_ns);
  return coro;

 fail:
  Py_XDECREF(formals);
  Py_XDECREF(nspec);
  Py_XDECREF(frame_ns);
  Py_XDECREF(kwargs);
  return NULL;
}


static PyMemberDef function_members[] = {
  { "code", T_OBJECT, offsetof(KosFunction, code), READONLY, NULL },
  { "base_ns", T_OBJECT, offsetof(KosFunction, base_ns), READONLY, NULL },
  { "formals_spec", T_OBJECT, offsetof(KosFunction, formals_spec), READONLY, NULL },
  { "formals", T_OBJECT, offsetof(KosFunction, formals), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


PyTypeObject KosFunctionType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Function",
  .tp_basicsize = sizeof(KosFunction),
  .tp_dealloc = function_dealloc,
  .tp_repr = (reprfunc) NULL,
  .tp_call = function_call,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Callable(Code, base_namespace, FormalsSpec). Calling with "
             "(*args, **kwargs) binds formals and returns a Coroutine.",
  .tp_init = (initproc) function_init,
  .tp_new = PyType_GenericNew,
  .tp_members = function_members,
};


PyObject *KosExpression_New(PyObject *code, PyObject *ns) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "Expression code must be Code, not %R", code);
    return NULL;
  }
  if (! ns) {
    PyErr_SetString(PyExc_TypeError, "Expression namespace is required");
    return NULL;
  }
  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError, "Expression namespace must be Namespace,"
                 " not %R", ns);
    return NULL;
  }

  PyObject *self = PyType_GenericNew(&KosExpressionType, NULL, NULL);
  if (! self)
    return NULL;

  KosExpression *ex = (KosExpression *) self;
  Py_INCREF(code);
  Py_INCREF(ns);
  ex->code = code;
  ex->ns = ns;
  return self;
}


PyObject *KosFunction_NewFromFormals(PyObject *code,
                                      PyObject *formals,
                                      PyObject *parent_ns) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "Function code must be Code, not %R", code);
    return NULL;
  }
  if (! KosFormals_Check(formals)) {
    PyErr_Format(PyExc_TypeError, "Function formals must be Formals, not %R",
                 formals);
    return NULL;
  }
  if (! parent_ns) {
    PyErr_SetString(PyExc_TypeError, "Function parent namespace is required");
    return NULL;
  }
  if (! KosNamespace_Check(parent_ns)) {
    PyErr_Format(PyExc_TypeError, "Function parent namespace must be"
                 " Namespace, not %R", parent_ns);
    return NULL;
  }

  PyObject *self = PyType_GenericNew(&KosFunctionType, NULL, NULL);
  if (! self)
    return NULL;

  KosFunction *fn = (KosFunction *) self;
  Py_INCREF(code);
  Py_INCREF(formals);
  Py_INCREF(parent_ns);
  fn->code = code;
  fn->base_ns = parent_ns;
  fn->formals_spec = NULL;
  fn->formals = formals;
  return self;
}


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosExpressionType))
    return -1;

  if (PyType_Ready(&KosFunctionType))
    return -1;

  if (PyDict_SetItemString(dict, "Expression",
                           (PyObject *) &KosExpressionType))
    return -1;

  if (PyDict_SetItemString(dict, "Function",
                           (PyObject *) &KosFunctionType))
    return -1;

  return 0;
}


int koskari_func_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
