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


/* === Expression === */


extern PyTypeObject KosExpressionType;


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *ns;
} KosExpression;


/** Borrowed reference. Do not Py_DECREF the result. */
#define KosExpression_CODE(expr) (((KosExpression *) (expr))->code)

/** Borrowed reference. Do not Py_DECREF the result. */
#define KosExpression_NS(expr)   (((KosExpression *) (expr))->ns)


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


PyObject *KosFunctionSpec_New(PyObject *name, PyObject *formals_spec,
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
PyObject *KosFunction_New(PyObject *func_spec,
                          PyObject *kw_defaults,
                          PyObject *parent_namespace);


/* === Function === */


extern PyTypeObject KosFunctionType;


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *base_ns;
  PyObject *formals;
  PyObject *name;   /* proper name for self-reference and repr, or NULL */
} KosFunction;


/** Borrowed reference. Do not Py_DECREF the result. */
#define KosFunction_CODE(func) (((KosFunction *) (func))->code)

#define KosFunction_NAME(func) (((KosFunction *) (func))->name)

#define KosFunction_BASE_NS(func) (((KosFunction *) (func))->base_ns)

#define KosFunction_FORMALS(func) (((KosFunction *) (func))->formals)


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
PyObject *KosFunction_NewFromFormals(PyObject *code,
                                     PyObject *formals,
                                     PyObject *parent_ns);


/**
   Call the function with no arguments.
*/
PyObject *KosFunction_CallNoArgs(PyObject *func);


/**
   Call the function with the given args and kwds.
*/
PyObject *KosFunction_Call(PyObject *func,
                           PyObject *args, PyObject *kwds);


/**
   Prepare the call namespace for a KosFunction with args/kwds applied.
   Used by the evaluator to flatten Koskari calls without creating a coroutine.
   Returns new reference to frame namespace, or NULL on error.
*/
PyObject *KosFunction_PrepareCallNamespace(PyObject *func,
                                            PyObject *args,
                                            PyObject *kwds);


/**
   Return the function's code (new reference). Caller must Py_DECREF when done.
*/
PyObject *KosFunction_GetCode(PyObject *func);




/* === Run code as coroutine === */


/**
   Create a coroutine that runs the given Code with the given namespace.

   The coroutine is not started; the caller drives it via send(None) or
   await. code must be a KosCode; ns is the initial frame namespace
   (e.g. a KosNamespace or dict), reference is borrowed.

   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosCoroutine_NewFromCode(PyObject *code, PyObject *ns);


/* === KosCoroutine === */


/**
   KosCoroutine wraps a KosContext for Python's coroutine protocol.

   States: KOS_CORO_NEW, KOS_CORO_RUNNING, KOS_CORO_WAITING_PY,
   KOS_CORO_YIELDED (suspended due to yield, awaiting send()),
   KOS_CORO_DONE.
*/

extern PyTypeObject KosCoroutineType;


typedef enum {
  KOS_CORO_NEW = 0,
  KOS_CORO_RUNNING,
  KOS_CORO_WAITING_PY,
  KOS_CORO_YIELDED,
  KOS_CORO_DONE,
} KosCoroState;


typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  KosCoroState state;
  PyObject *pending_await;  /* yielded object to bubble outward */
  int in_send;              /* re-entrancy guard */
} KosCoroutine;


#define KosCoroutine_Check(obj)                                         \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCoroutineType))

#define KosCoroutine_CheckExact(obj)                    \
  ((obj) && ((obj)->ob_type == &KosCoroutineType))


PyObject *KosCoroutine_New(KosContext *ctx);


/* === KosPyDelimitedContinuation === */


/**
   KosPyDelimitedContinuation wraps a KosContinuation captured by shift.

   Cannot be called from Python; application only via CALL inside koskari_eval.
*/

extern PyTypeObject KosPyDelimitedContinuationType;


typedef struct {
  PyObject_HEAD
  KosContinuation *captured;
} KosPyDelimitedContinuation;


#define KosPyDelimitedContinuation_Check(obj)                           \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyDelimitedContinuationType))


PyObject *KosPyDelimitedContinuation_New(KosContinuation *captured);


/* === KosPyContinuation (undelimited, for future call/cc) === */


/**
   KosPyContinuation wraps an undelimited KosContinuation from call/cc.

   Python-callable: raises KoskariContinuationDead or KoskariContinuationApply.
*/

extern PyTypeObject KosPyContinuationType;


typedef struct {
  PyObject_HEAD
  KosContinuation *captured;
} KosPyContinuation;


#define KosPyContinuation_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyContinuationType))

#define KosPyContinuation_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == &KosPyContinuationType))


PyObject *KosPyContinuation_New(KosContinuation *captured);


/* === KosPyAsyncGen === */


/**
   KosPyAsyncGen wraps a KosContext for Python's async generator protocol.

   Similar to KosCoroutine but yields values via __anext__.
*/

extern PyTypeObject KosPyAsyncGenType;


typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  int in_progress;   /* anext in progress */
  int closed;        /* generator closed */
} KosPyAsyncGen;


#define KosPyAsyncGen_Check(obj)                                        \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyAsyncGenType))

#define KosPyAsyncGen_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == &KosPyAsyncGenType))


PyObject *KosPyAsyncGen_New(KosContext *ctx);


/* === KosPyAsyncGenASend === */


/**
   KosPyAsyncGenASend is the awaitable returned by __anext__ on an async gen.

   Each __anext__ call creates one of these, which drives the generator
   context until a yield or completion.
*/

extern PyTypeObject KosPyAsyncGenASendType;


typedef struct {
  PyObject_HEAD
  KosPyAsyncGen *agen;
  PyObject *send_value;   /* value to send into generator */
  int is_throw;           /* if true, throw send_value as exception */
  PyObject *throw_exc;    /* exception to throw (if is_throw) */
} KosPyAsyncGenASend;


#define KosPyAsyncGenASend_Check(obj)                                   \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyAsyncGenASendType))

#define KosPyAsyncGenASend_CheckExact(obj)              \
  ((obj) && ((obj)->ob_type == &KosPyAsyncGenASendType))


PyObject *KosPyAsyncGenASend_New(KosPyAsyncGen *agen, PyObject *send_value);


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod);
int koskari_func_clear(PyObject *mod);


#endif


// The end.
