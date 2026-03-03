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


#ifndef KOSKARI_FUNC_H
#define KOSKARI_FUNC_H


#include <Python.h>
#include "callable.h"


/* === Expression === */


extern KosCallableTypeObject KosExpressionType;


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *ns;
} KosExpression;


/** Borrowed reference. Do not Py_DECREF the result. */
#define KosExpression_CODE(expr) (((KosExpression *) (expr))->code)

/** Borrowed reference. Do not Py_DECREF the result. */
#define KosExpression_NS(expr)   (((KosExpression *) (expr))->ns)

#define KosExpression_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == KOS_CALLABLE_PYTYPE(KosExpressionType)))


/**
   Create an Expression (code, ns). Used by Code.expression(parent_ns).

   Calling the returned Expression with () runs the code in the namespace
   and returns a Coroutine.

   :param code: KosCode instance
   :param ns: KosNamespace instance (parent/global namespace)
   :return: new Expression, or NULL on error
*/
PyObject *KosExpression_New(PyObject *code, PyObject *ns);


/**
   Run the expression's code in its namespace and return a new Coroutine.

   :param expr: KosExpression instance
   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosExpression_Eval(PyObject *expr);


/**
   Return the expression's code (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosExpression_GetCode(PyObject *expr);

/**
   Return the expression's namespace (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosExpression_GetNs(PyObject *expr);


/* === FunctionSpec === */


extern PyTypeObject KosFunctionSpecType;


typedef struct KosFunctionSpec {
  PyObject_HEAD

  PyObject *name;           /* PyUnicode or NULL */
  PyObject *formals_spec;   /* KosFormalsSpec */
  PyObject *code;           /* KosCode */
} KosFunctionSpec;


PyObject *KosFunctionSpec_New(
      PyObject *name,
      PyObject *formals_spec,
      PyObject *code);


#define KosFunctionSpec_NAME(fspec)             \
  (((KosFunctionSpec *) (fspec))->name)

#define KosFunctionSpec_FORMALS_SPEC(fspec)    \
  (((KosFunctionSpec *) (fspec))->formals_spec)

#define KosFunctionSpec_CODE(fspec)            \
  (((KosFunctionSpec *) (fspec))->code)

#define KosFunctionSpec_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosFunctionSpecType))


/**
   Create a Function from a FunctionSpec and keyword defaults.
   Used by BUILD_FUNCTION: formals = KosFormals_New(fspec->formals_spec,
   kw_defaults), then KosFunction_NewFromFormals(fspec->code, formals,
   parent_ns).

   :param func_spec: KosFunctionSpec instance
   :param kw_defaults: PyDict of name -> default value, or NULL
   :param parent_namespace: KosNamespace (current frame NS)
   :return: new KosFunction, or NULL on error
*/
PyObject *KosFunction_New(
      PyObject *func_spec,
      PyObject *kw_defaults,
      PyObject *parent_namespace);


/* === Function === */


extern KosCallableTypeObject KosFunctionType;


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *base_ns;
  PyObject *formals;
  PyObject *name;           /* proper name for self-reference and repr, or NULL */
  vectorcallfunc vectorcall; /* for Py_TPFLAGS_HAVE_VECTORCALL; always set */
} KosFunction;


/** Borrowed reference. Do not Py_DECREF the result. */
#define KosFunction_CODE(func) (((KosFunction *) (func))->code)

#define KosFunction_NAME(func) (((KosFunction *) (func))->name)

#define KosFunction_BASE_NS(func) (((KosFunction *) (func))->base_ns)

#define KosFunction_FORMALS(func) (((KosFunction *) (func))->formals)

#define KosFunction_VECTORCALL_OFFSET offsetof(KosFunction, vectorcall)

#define KosFunction_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == KOS_CALLABLE_PYTYPE(KosFunctionType)))


/* === KosBoundMethod === */


extern KosCallableTypeObject KosBoundMethodType;


typedef struct {
  PyObject_HEAD
  PyObject *func;
  PyObject *self;
} KosBoundMethod;


#define KosBoundMethod_FUNC(bm) (((KosBoundMethod *) (bm))->func)
#define KosBoundMethod_SELF(bm) (((KosBoundMethod *) (bm))->self)

#define KosBoundMethod_CheckExact(obj)                           \
  ((obj) && ((obj)->ob_type == KOS_CALLABLE_PYTYPE(KosBoundMethodType)))


/**
   Create a KosBoundMethod (func, self). Used by KosFunction.__get__
   when accessed via instance. Caller must Py_DECREF when done.
*/
PyObject *KosBoundMethod_New(PyObject *func, PyObject *self);


/**
   Create a Function from (code, formals, parent_ns). Used by
   Code.function(formals, parent_ns). Same assembly as sibilant-ceval
   ByteCode.function(formals, parent); calling the result returns a
   Coroutine.

   :param code: KosCode instance
   :param formals: KosFormals instance (resolved from FormalsSpec + defaults)
   :param parent_ns: KosNamespace instance (base namespace)
   :return: new Function, or NULL on error
*/
PyObject *KosFunction_NewFromFormals(
      PyObject *code,
      PyObject *formals,
      PyObject *parent_ns);


/**
   Call the function with no arguments.
*/
PyObject *KosFunction_CallNoArgs(PyObject *func);


/**
   Call the function with the given args and kwds.
*/
PyObject *KosFunction_Call(
      PyObject *func,
      PyObject *args,
      PyObject *kwds);


/**
   Prepare the call namespace for a KosFunction with args/kwds applied.
   Used by the evaluator to flatten Koskari calls without creating a coroutine.
   Returns new reference to frame namespace, or NULL on error.
*/
PyObject *KosFunction_PrepareCallNamespace(
      PyObject *func,
      PyObject *args,
      PyObject *kwds);


/**
   Prepare the call namespace using the vectorcall convention. args is a
   C array of nargs positional arguments followed by keyword argument values;
   kwnames is a tuple of keyword names (may be NULL for positional-only).
   Returns new reference to frame namespace, or NULL on error.
*/
PyObject *KosFunction_PrepareCallNamespaceFromVector(
      PyObject *func,
      PyObject *const *args,
      Py_ssize_t nargs,
      PyObject *kwnames);


/**
   Call the function using the vectorcall convention, returning a new Coroutine.
   nargsf encodes the positional count (use PyVectorcall_NARGS to extract).
   kwnames is a tuple of keyword names corresponding to values at args[npos..].
*/
PyObject *KosFunction_VectorCall(
      PyObject *func,
      PyObject *const *args,
      size_t nargsf,
      PyObject *kwnames);


/**
   Return the function's code (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosFunction_GetCode(PyObject *func);


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod);
int koskari_func_clear(PyObject *mod);


#endif


// The end.
