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
#include "debug.h"


/* === KosExpression: callable (code, ns) -> coroutine on __call__ === */


static int expression_init(
    KosExpression *self,
    PyObject *args,
    PyObject *kwargs) {

  (void) kwargs;
  PyObject *code = NULL;
  PyObject *ns = NULL;

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
  KosExpression_CODE(self) = code;

  Py_INCREF(ns);
  KosExpression_NS(self) = ns;

  return 0;
}


static void expression_dealloc(PyObject *self) {
  Py_CLEAR(KosExpression_CODE(self));
  Py_CLEAR(KosExpression_NS(self));
  Py_TYPE(self)->tp_free(self);
}


PyObject *KosExpression_Eval(PyObject *self) {
  return KosCoroutine_NewFromCode(
    KosExpression_CODE(self),
    KosExpression_NS(self)
  );
}


PyObject *KosExpression_GetCode(PyObject *self) {
  Py_INCREF(KosExpression_CODE(self));
  return KosExpression_CODE(self);
}


PyObject *KosExpression_GetNs(PyObject *self) {
  Py_INCREF(KosExpression_NS(self));
  return KosExpression_NS(self);
}


static PyObject *expression_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  if (args && PyTuple_GET_SIZE(args) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression() takes no arguments");
    return NULL;
  }

  if (kwargs && PyDict_Size(kwargs) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return NULL;
  }

  return KosExpression_Eval(self);
}


static PyMemberDef expression_members[] = {
  { "code", T_OBJECT, offsetof(KosExpression, code), READONLY, NULL },
  { "ns", T_OBJECT, offsetof(KosExpression, ns), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


KosCallableTypeObject KosExpressionType = {
  .type = {
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
    .tp_base = KOS_CALLABLE_PYTYPE(KosCallableType),
  },
  .tp_kos_call = kos_callable_expression_call,
  .tp_kos_tail_call = kos_callable_expression_tail_call,
};


/* === KosFunctionSpec: (name, formals_spec, code) for BUILD_FUNCTION === */


static PyMemberDef function_spec_members[] = {
  { "name", T_OBJECT,
    offsetof(KosFunctionSpec, name), READONLY,
    "name for function" },

  { "formals_spec", T_OBJECT,
    offsetof(KosFunctionSpec, formals_spec), READONLY,
    "formals spec for function" },

  { "code", T_OBJECT,
    offsetof(KosFunctionSpec, code), READONLY,
    "KosCode for function" },

  { NULL, },
};


static PyObject *function_spec_call(PyObject *self,
                                    PyObject *args, PyObject *kwds) {

  if (kwds) {
    PyErr_SetString(
      PyExc_TypeError,
      "FunctionSpec does not accept keyword arguments"
    );
    return NULL;
  }

  PyObject *defaults = NULL;
  PyObject *parent = NULL;

  if (! PyArg_ParseTuple(args, "OO:FunctionSpec", &defaults, &parent))
    return NULL;

  return KosFunction_New(self, defaults, parent);
}


static PyObject *function_spec_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  if (kwds) {
    PyErr_SetString(
      PyExc_TypeError,
      "FunctionSpec does not accept keyword arguments"
    );
    return NULL;
  }

  PyObject *name = NULL;
  PyObject *fspec = NULL;
  PyObject *code = NULL;

  if (! PyArg_ParseTuple(args, "OOO:FunctionSpec", &name, &fspec, &code))
    return NULL;

  return KosFunctionSpec_New(name, fspec, code);
}


static void function_spec_dealloc(PyObject *self) {
  Py_CLEAR(KosFunctionSpec_NAME(self));
  Py_CLEAR(KosFunctionSpec_FORMALS_SPEC(self));
  Py_CLEAR(KosFunctionSpec_CODE(self));
  Py_TYPE(self)->tp_free(self);
}


static int function_spec_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosFunctionSpec_NAME(self));
  Py_VISIT(KosFunctionSpec_FORMALS_SPEC(self));
  Py_VISIT(KosFunctionSpec_CODE(self));
  return 0;
}


static int function_spec_clear(PyObject *self) {
  Py_CLEAR(KosFunctionSpec_NAME(self));
  Py_CLEAR(KosFunctionSpec_FORMALS_SPEC(self));
  Py_CLEAR(KosFunctionSpec_CODE(self));
  return 0;
}


PyTypeObject KosFunctionSpecType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.FunctionSpec",
  .tp_basicsize = sizeof(KosFunctionSpec),
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_members = function_spec_members,
  .tp_new = function_spec_new,
  .tp_call = function_spec_call,
  .tp_dealloc = function_spec_dealloc,
  .tp_traverse = function_spec_traverse,
  .tp_clear = function_spec_clear,
};


PyObject *KosFunctionSpec_New(PyObject *name, PyObject *formals_spec,
                              PyObject *code) {

  if (! KosCode_Check(code)) {
    PyErr_Format(
      PyExc_TypeError,
      "function spec code must be Code, not %R",
      code
    );
    return NULL;
  }

  if (name == Py_None)
    name = NULL;

  if (! KosFormalsSpec_CheckExact(formals_spec)) {
    PyErr_Format(
      PyExc_TypeError,
      "function spec formals must be FormalsSpec, not %R",
      formals_spec
    );
    return NULL;
  }

  PyObject *self = PyType_GenericNew(&KosFunctionSpecType, NULL, NULL);
  if (! self)
    return NULL;

  KosFunctionSpec *spec = (KosFunctionSpec *) self;
  Py_XINCREF(name);
  KosFunctionSpec_NAME(spec) = name;

  Py_INCREF(formals_spec);
  KosFunctionSpec_FORMALS_SPEC(spec) = formals_spec;

  Py_INCREF(code);
  KosFunctionSpec_CODE(spec) = code;

  return self;
}


/* === KosFunction: callable (code, formals, base_ns) -> coroutine on __call__ === */


static int function_init(
    KosFunction *self,
    PyObject *args,
    PyObject *kwargs) {

  PyObject *code = NULL;
  PyObject *base_ns = NULL;
  PyObject *formals_spec = NULL;
  PyObject *formals = NULL;

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

  formals = KosFormals_New(formals_spec, NULL);
  if (! formals)
    return -1;

  KosFunction_FORMALS(self) = formals;

  Py_INCREF(code);
  KosFunction_CODE(self) = code;

  Py_INCREF(base_ns);
  KosFunction_BASE_NS(self) = base_ns;

  return 0;
}


static void function_dealloc(PyObject *self) {
  Py_CLEAR(KosFunction_NAME(self));
  Py_CLEAR(KosFunction_CODE(self));
  Py_CLEAR(KosFunction_BASE_NS(self));
  Py_CLEAR(KosFunction_FORMALS(self));
  Py_TYPE(self)->tp_free(self);
}


/**
   Obtain the call namespace for a function: formals' namespace spec
   (with code's nspec as base so code-local names like named-lambda self
   are in scope) applied to base_ns.  Returns new reference or NULL on error.
*/
static PyObject *func_obtain_call_namespace(KosFunction *fn) {
  PyObject *base_nspec = KosCode_NSPEC(KosFunction_CODE(fn));
  PyObject *nspec = KosFormals_NamespaceSpec(KosFunction_FORMALS(fn), base_nspec);
  if (! nspec)
    return NULL;
  PyObject *frame_ns = KosNamespace_FromSpec(nspec, KosFunction_BASE_NS(fn));
  Py_DECREF(nspec);
  return frame_ns;
}


PyObject *KosFunction_Call(PyObject *self,
                           PyObject *args, PyObject *kwds) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    goto fail;

  if (KosFormals_Apply(KosFunction_FORMALS(fn), frame_ns, args, kwds) < 0)
    goto fail;

  if (KosFunction_NAME(fn) != NULL) {
    if (KosNamespace_Store(frame_ns, KosFunction_NAME(fn), self) < 0)
      goto fail;
  }

  coro = KosCoroutine_NewFromCode(KosFunction_CODE(fn), frame_ns);
  Py_DECREF(frame_ns);
  return coro;

 fail:
  Py_XDECREF(frame_ns);
  return NULL;
}


PyObject *KosFunction_PrepareCallNamespace(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    return NULL;

  if (KosFormals_Apply(KosFunction_FORMALS(fn), frame_ns, args, kwds) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  if (KosFunction_NAME(fn) != NULL) {
    if (KosNamespace_Store(frame_ns, KosFunction_NAME(fn), self) < 0) {
      Py_DECREF(frame_ns);
      return NULL;
    }
  }
  return frame_ns;
}


PyObject *KosFunction_GetCode(PyObject *self) {
  Py_INCREF(KosFunction_CODE(self));
  return KosFunction_CODE(self);
}


PyObject *KosFunction_CallNoArgs(PyObject *self) {
  return KosFunction_Call(self, NULL, NULL);
}


static PyObject *function_repr(PyObject *self) {
  KosFunction *fn = (KosFunction *) self;
  PyObject *name = KosFunction_NAME(fn);
  if (name != NULL)
    return PyUnicode_FromFormat("<Function %R>", name);
  name = KosCode_NAME(KosFunction_CODE(fn));
  if (name != NULL)
    return PyUnicode_FromFormat("<Function %R>", name);
  return PyUnicode_FromString("<Function <lambda>>");
}


static PyObject *function_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  return KosFunction_Call(self, args, kwargs);
}


static PyMemberDef function_members[] = {
  { "code", T_OBJECT, offsetof(KosFunction, code), READONLY, NULL },
  { "base_ns", T_OBJECT, offsetof(KosFunction, base_ns), READONLY, NULL },
  { "formals", T_OBJECT, offsetof(KosFunction, formals), READONLY, NULL },
  { "name", T_OBJECT, offsetof(KosFunction, name), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


KosCallableTypeObject KosFunctionType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Function",
  .tp_basicsize = sizeof(KosFunction),
  .tp_dealloc = function_dealloc,
  .tp_repr = function_repr,
  .tp_call = function_call,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Callable(Code, base_namespace, FormalsSpec). Calling with "
             "(*args, **kwargs) binds formals and returns a Coroutine.",
  .tp_init = (initproc) function_init,
  .tp_new = PyType_GenericNew,
  .tp_members = function_members,
    .tp_base = KOS_CALLABLE_PYTYPE(KosCallableType),
  },
  .tp_kos_call = kos_callable_function_call,
  .tp_kos_tail_call = kos_callable_function_tail_call,
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

  PyObject *self = PyType_GenericNew(KOS_CALLABLE_PYTYPE(KosExpressionType), NULL, NULL);
  if (! self)
    return NULL;

  Py_INCREF(code);
  KosExpression_CODE(self) = code;

  Py_INCREF(ns);
  KosExpression_NS(self) = ns;

  return self;
}


PyObject *KosFunction_NewFromFormals(
    PyObject *code,
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

  PyObject *self = PyType_GenericNew(KOS_CALLABLE_PYTYPE(KosFunctionType), NULL, NULL);
  if (! self)
    return NULL;

  Py_INCREF(code);
  KosFunction_CODE(self) = code;

  Py_INCREF(formals);
  KosFunction_FORMALS(self) = formals;

  Py_INCREF(parent_ns);
  KosFunction_BASE_NS(self) = parent_ns;

  KosFunction_NAME(self) = NULL;

  return self;
}


PyObject *KosFunction_New(
    PyObject *func_spec,
    PyObject *kw_defaults,
    PyObject *parent_namespace) {

  if (! KosFunctionSpec_CheckExact(func_spec)) {
    PyErr_Format(PyExc_TypeError, "KosFunction_New requires FunctionSpec,"
                 " not %R", func_spec);
    return NULL;
  }

  PyObject *formals_spec = KosFunctionSpec_FORMALS_SPEC(func_spec);
  PyObject *code = KosFunctionSpec_CODE(func_spec);

  PyObject *formals = KosFormals_New(formals_spec, kw_defaults);
  if (! formals)
    return NULL;

  PyObject *fn = KosFunction_NewFromFormals(code, formals, parent_namespace);
  Py_DECREF(formals);
  if (fn != NULL) {
    PyObject *spec_name = KosFunctionSpec_NAME(func_spec);
    if (spec_name != NULL) {
      KosFunction_NAME(fn) = spec_name;
      Py_INCREF(spec_name);
    }
  }
  return fn;
}


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_CALLABLE_PYTYPE(KosExpressionType)) ||
      PyType_Ready(&KosFunctionSpecType) ||
      PyType_Ready(KOS_CALLABLE_PYTYPE(KosFunctionType)))
    return -1;

  if (PyDict_SetItemString(dict, "Expression",
                           (PyObject *) KOS_CALLABLE_PYTYPE(KosExpressionType)))
    return -1;

  if (PyDict_SetItemString(dict, "FunctionSpec",
                           (PyObject *) &KosFunctionSpecType))
    return -1;

  if (PyDict_SetItemString(dict, "Function",
                           (PyObject *) KOS_CALLABLE_PYTYPE(KosFunctionType)))
    return -1;

  return 0;
}


int koskari_func_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
