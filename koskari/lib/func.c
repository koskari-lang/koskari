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
*/


#include "lib.h"
#include "debug.h"


static PyObject *boundmethod_resolve_function(PyObject *func);

static Py_ssize_t function_compute_simple_arity(
        PyObject *code,
        PyObject *formals);


static PyObject *func_build_template_ns(PyObject *code, PyObject *formals,
                                        PyObject *parent_ns);


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
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosExpression_CODE(self));
  Py_CLEAR(KosExpression_NS(self));
  Py_TYPE(self)->tp_free(self);
}


/* An expression holds a Code and a namespace, both of which can sit in
   a cycle back to it, so it takes part in collection. */
static int expression_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosExpression_CODE(self));
  Py_VISIT(KosExpression_NS(self));
  return 0;
}


static int expression_clear(PyObject *self) {
  Py_CLEAR(KosExpression_CODE(self));
  Py_CLEAR(KosExpression_NS(self));
  return 0;
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


static PyMemberDef expression_members[] = {
  { "code", T_OBJECT, offsetof(KosExpression, code), READONLY, NULL },
  { "ns", T_OBJECT, offsetof(KosExpression, ns), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


/* === KosExpression install slots === */


/**
   Install an Expression: yielding code completes in hand with a new
   AsyncGenerator over the expression's namespace; any other code takes
   a frame.
*/
static KosOpResult kos_expression_install_core(
    KosEvalState *es,
    PyObject *callable,
    int tail_call) {

  KosCode *code = (KosCode *) KosExpression_CODE(callable);
  PyObject *ns = KosExpression_NS(callable);

  Py_INCREF(ns);  /* NewFromCodeAndNs and InstallCallFrame steal it */
  if (code && KosCode_YIELDS(code)) {
    PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, ns);
    return agen ? EVAL_RETURN(agen) : EVAL_RAISE();
  }
  return KosEvalState_InstallCallFrame(es, code, ns, tail_call);
}


static KosOpResult kos_vinstall_expression(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  (void) args;

  if (nargs != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
    return EVAL_RAISE();
  }
  if (kwnames && PyTuple_GET_SIZE(kwnames) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return EVAL_RAISE();
  }

  return kos_expression_install_core(es, callable, tail_call);
}


/* === KosFunction install slots === */


/**
   Install a KosFunction call. A simple call (exact arity against plain
   positional formals, no keywords, no self-bind) binds by slot index:
   not in tail position it takes the function's parked frame when one
   is there, else pushes a fresh one; in tail position it refills the
   caller's frame. Any other call binds through the general formals
   machine. A generator function completes in hand with its new
   AsyncGenerator. Every installed frame is tagged with the function so
   its release can park it.
*/
static KosOpResult kos_vinstall_function(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  int simple = ! kwnames
      && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
      && KosFunction_BIND_SELF_STATE(callable) == 2;
  int yields = code && KosCode_YIELDS(code);
  PyObject *frame_ns;
  KosOpResult r;

  /* A generator function never takes a frame: it binds and completes
     with the new AsyncGenerator, so the gate stands ahead of every
     frame arm below. */
  if (simple && ! tail_call && ! yields) {
    KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);

    if (kos_eval_check_frame_room(es, KosEvalState_FRAME(es), 1) < 0)
      return EVAL_RAISE();

    if (cached) {
      for (Py_ssize_t i = 0; i < nargs; i++)
        Py_INCREF(args[i]);
      (void) KosFormals_SimpleStealApply(KosDynFrame_NS(cached), args,
                                         nargs);
      KosFunction_CACHED_FRAME(callable) = NULL;
      kos_dyn_push_ready_frame(es, cached);
      Py_INCREF(callable);
      KosDynFrame_FN(cached) = callable;
      KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
      return kos_call_installed(es);
    }

    frame_ns = KosFunction_SimpleCallNamespaceFromVector(callable, args,
                                                         nargs);
    if (! frame_ns)
      return EVAL_RAISE();
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_VECTOR);
    if (! kos_dyn_push_frame_unchecked(es, code, frame_ns))
      return EVAL_RAISE();
    Py_INCREF(callable);
    KosDynFrame_FN(KosEvalState_FRAME(es)) = callable;
    return kos_call_installed(es);
  }

  if (simple) {
    frame_ns = KosFunction_SimpleCallNamespaceFromVector(callable, args,
                                                         nargs);
  } else {
    frame_ns = KosFunction_PrepareCallNamespaceFromVector(
        callable, args, nargs, kwnames);
  }
  if (! frame_ns)
    return EVAL_RAISE();

  if (yields) {
    PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, frame_ns);
    return agen ? EVAL_RETURN(agen) : EVAL_RAISE();
  }

  r = KosEvalState_InstallCallFrame(es, code, frame_ns, tail_call);
  if (r.kind != KOS_RES_RAISE) {
    Py_INCREF(callable);
    KosDynFrame_FN(KosEvalState_FRAME(es)) = callable;
  }
  return r;
}


/**
   kc_pycall worker: an Expression takes no arguments; evaluate it.
*/
static PyObject *expression_pycall(
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


KosEvaluatedTypeObject KosExpressionType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Expression",
  .tp_basicsize = sizeof(KosExpression),
  .tp_dealloc = expression_dealloc,
  .tp_traverse = expression_traverse,
  .tp_clear = expression_clear,
  .tp_repr = (reprfunc) NULL,
  .tp_call = KosCallAdapt_CallAsync,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Callable(Code, namespace). Calling with () returns a Coroutine.",
  .tp_init = (initproc) expression_init,
  .tp_new = PyType_GenericNew,
  .tp_members = expression_members,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kos_vinstall_expression,
    .kc_pycall = expression_pycall,
    .kc_pyvectorcall = NULL,
  },
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
                                    PyObject *args, const PyObject *kwds) {

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
    const PyObject *kwds) {

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
  PyObject_GC_UnTrack(self);
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
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_members = function_spec_members,
  .tp_new = (newfunc) function_spec_new,
  .tp_call = (ternaryfunc) function_spec_call,
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


#include "func/func_inspect.c"


static PyObject *
function_get_signature(PyObject *self, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  (void) closure;

  if (! fn->inspect_signature) {
    PyObject *sig = kos_function_compute_signature(self);
    if (! sig)
      return NULL;
    fn->inspect_signature = sig;
  }

  Py_INCREF(fn->inspect_signature);
  return fn->inspect_signature;
}


static PyObject *
function_get_annotations(PyObject *self, void *closure) {

  (void) closure;

  PyObject *ann = kos_function_realize_annotations((KosFunction *) self);
  if (! ann)
    return NULL;

  Py_INCREF(ann);
  return ann;
}


static PyObject *
boundmethod_get_annotations(PyObject *self, void *closure) {

  PyObject *func = KosBoundMethod_FUNC(self);
  PyObject *resolved = boundmethod_resolve_function(func);
  PyObject *result;

  (void) closure;

  if (! resolved) {
    if (KosDemand_Check(func))
      return PyObject_GetAttrString(func, "__annotations__");
    return NULL;
  }

  result = function_get_annotations(resolved, NULL);
  Py_DECREF(resolved);
  return result;
}


static PyObject *
function_get_name_dunder(PyObject *self, void *closure) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *name;
  PyObject *cn;

  (void) closure;

  name = fn->name_override ? fn->name_override : KosFunction_NAME(fn);
  if (name != NULL) {
    Py_INCREF(name);
    return name;
  }

  cn = KosCode_NAME(KosFunction_CODE(fn));
  if (cn != NULL) {
    Py_INCREF(cn);
    return cn;
  }

  return PyUnicode_FromString("<lambda>");
}


/*
  __name__ and __qualname__ writes land in metadata override slots, never
  in `name`: self-binding keys off `name` at call time (see
  kos_function_bind_self_if_local), so the semantic name stays fixed
  after construction while wraps-style renames remain visible to
  introspection.
*/
static int
function_set_name_dunder(PyObject *self, PyObject *val, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  (void) closure;

  if (! val || ! PyUnicode_Check(val)) {
    PyErr_SetString(PyExc_TypeError,
                    "__name__ must be set to a string object");
    return -1;
  }

  Py_INCREF(val);
  Py_XSETREF(fn->name_override, val);
  return 0;
}


static PyObject *
function_get_qualname_dunder(PyObject *self, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  if (fn->qualname_override) {
    Py_INCREF(fn->qualname_override);
    return fn->qualname_override;
  }

  return function_get_name_dunder(self, closure);
}


static int
function_set_qualname_dunder(PyObject *self, PyObject *val, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  (void) closure;

  if (! val || ! PyUnicode_Check(val)) {
    PyErr_SetString(PyExc_TypeError,
                    "__qualname__ must be set to a string object");
    return -1;
  }

  Py_INCREF(val);
  Py_XSETREF(fn->qualname_override, val);
  return 0;
}


static PyObject *
function_get_module_dunder(PyObject *self, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  (void) closure;

  if (fn->module_override) {
    Py_INCREF(fn->module_override);
    return fn->module_override;
  }

  Py_INCREF(Py_None);
  return Py_None;
}


static int
function_set_module_dunder(PyObject *self, PyObject *val, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  (void) closure;

  Py_XINCREF(val);
  Py_XSETREF(fn->module_override, val);
  return 0;
}


static int
function_set_annotations(PyObject *self, PyObject *val, void *closure) {

  KosFunction *fn = (KosFunction *) self;

  (void) closure;

  if (! val || ! PyDict_Check(val)) {
    PyErr_SetString(PyExc_TypeError,
                    "__annotations__ must be set to a dict object");
    return -1;
  }

  Py_INCREF(val);
  Py_XSETREF(fn->annotations_cache, val);

  /* a cached signature was derived from the previous annotations */
  Py_CLEAR(fn->inspect_signature);
  return 0;
}


static PyGetSetDef function_getsets[] = {
  { "__signature__", (getter) function_get_signature, NULL, NULL, NULL },
  { "__annotations__", (getter) function_get_annotations,
    (setter) function_set_annotations, NULL, NULL },
  { "__name__", (getter) function_get_name_dunder,
    (setter) function_set_name_dunder, NULL, NULL },
  { "__qualname__", (getter) function_get_qualname_dunder,
    (setter) function_set_qualname_dunder, NULL, NULL },
  { "__module__", (getter) function_get_module_dunder,
    (setter) function_set_module_dunder, NULL, NULL },
  { "__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL },
  { NULL, }
};


static PyObject *
boundmethod_get_signature(PyObject *self, void *closure) {

  PyObject *func = KosBoundMethod_FUNC(self);
  PyObject *resolved = boundmethod_resolve_function(func);
  PyObject *full = NULL;
  PyObject *out = NULL;

  (void) closure;

  if (! resolved) {
    if (KosDemand_Check(func))
      return PyObject_GetAttrString(func, "__signature__");
    return NULL;
  }

  full = function_get_signature(resolved, NULL);
  Py_DECREF(resolved);
  if (! full)
    return NULL;

  if (kos_inspect_load() < 0) {
    Py_DECREF(full);
    return NULL;
  }

  out = kos_signature_drop_first(full);
  Py_DECREF(full);
  return out;
}


/*
  Identity metadata delegates to the callable this method binds, matching
  CPython bound-method behavior: reads forward to __func__, writes are
  refused (the getset has no setter). Plain attribute access covers
  Function and Demand alike, since both expose these names.
*/
static PyObject *
boundmethod_get_func_attr(PyObject *self, void *closure) {

  const char *name = (const char *) closure;
  PyObject *func = KosBoundMethod_FUNC(self);

  return PyObject_GetAttrString(func, name);
}


static PyGetSetDef boundmethod_getsets[] = {
  { "__signature__", (getter) boundmethod_get_signature, NULL, NULL, NULL },
  { "__annotations__", (getter) boundmethod_get_annotations, NULL, NULL, NULL },
  { "__name__", (getter) boundmethod_get_func_attr, NULL, NULL,
    (void *) "__name__" },
  { "__qualname__", (getter) boundmethod_get_func_attr, NULL, NULL,
    (void *) "__qualname__" },
  { "__doc__", (getter) boundmethod_get_func_attr, NULL, NULL,
    (void *) "__doc__" },
  { "__module__", (getter) boundmethod_get_func_attr, NULL, NULL,
    (void *) "__module__" },
  { NULL, }
};


/* === KosBoundMethod: bound method (func, self) via descriptor protocol === */


static PyObject *boundmethod_resolve_function(PyObject *func) {

  if (KosFunction_CheckExact(func)) {
    Py_INCREF(func);
    return func;
  }

  if (KosDemand_Check(func)) {
    PyObject *inner = KosDemand_GetWrapped(func);

    if (inner && KosFunction_CheckExact(inner)) {
      Py_INCREF(inner);
      return inner;
    }
  }

  return NULL;
}


static KosOpResult kos_vinstall_boundmethod(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  PyObject *func = KosBoundMethod_FUNC(callable);
  PyObject *self = KosBoundMethod_SELF(callable);
  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
  Py_ssize_t total_slots = 1 + nargs + nkw;
  PyObject **new_args = NULL;
  KosOpResult r;

  if (! KosCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError,
                    "KosBoundMethod eval requires a KosCallable func");
    return EVAL_RAISE();
  }

  new_args = PyMem_New(PyObject *, (size_t) total_slots);
  if (! new_args) {
    PyErr_NoMemory();
    return EVAL_RAISE();
  }
  new_args[0] = self;
  for (Py_ssize_t i = 0; i < nargs; i++)
    new_args[1 + i] = args[i];
  for (Py_ssize_t i = 0; i < nkw; i++)
    new_args[1 + nargs + i] = args[nargs + i];
  r = KosEvalState_CallResult(es, func, new_args, nargs + 1, kwnames,
                              tail_call, NULL, NULL);
  PyMem_Del(new_args);
  return r;
}




static void boundmethod_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosBoundMethod_FUNC(self));
  Py_CLEAR(KosBoundMethod_SELF(self));
  Py_TYPE(self)->tp_free(self);
}


/* A bound method owns its function and receiver; an instance that
   stores one of its own bound methods is a cycle, so it takes part in
   collection. */
static int boundmethod_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosBoundMethod_FUNC(self));
  Py_VISIT(KosBoundMethod_SELF(self));
  return 0;
}


static int boundmethod_clear(PyObject *self) {
  Py_CLEAR(KosBoundMethod_FUNC(self));
  Py_CLEAR(KosBoundMethod_SELF(self));
  return 0;
}


static PyMemberDef boundmethod_members[] = {
  { "__func__", T_OBJECT, offsetof(KosBoundMethod, func), READONLY, NULL },
  { "__self__", T_OBJECT, offsetof(KosBoundMethod, self), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


static PyObject *boundmethod_repr(PyObject *self) {
  PyObject *func = KosBoundMethod_FUNC(self);
  PyObject *resolved = boundmethod_resolve_function(func);
  PyObject *name = NULL;
  PyObject *result = NULL;

  if (resolved) {
    name = KosFunction_NAME((KosFunction *) resolved);
    if (! name)
      name = KosCode_NAME(KosFunction_CODE(resolved));
  }

  result = PyUnicode_FromFormat(
      "<bound method %R of %R>",
      name ? name : (PyObject *) Py_None,
      KosBoundMethod_SELF(self));
  Py_XDECREF(resolved);
  return result;
}


/**
   kc_pycall worker: prepend self and follow the wrapped callable
   through the generic iteration-node entry.
*/
static PyObject *boundmethod_pycall(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  PyObject *bound_self = KosBoundMethod_SELF(self);
  PyObject *bound_args;
  Py_ssize_t n = args ? PyTuple_GET_SIZE(args) : 0;

  bound_args = PyTuple_New(1 + n);
  if (! bound_args)
    return NULL;
  Py_INCREF(bound_self);
  PyTuple_SET_ITEM(bound_args, 0, bound_self);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *item = PyTuple_GET_ITEM(args, i);
    Py_INCREF(item);
    PyTuple_SET_ITEM(bound_args, 1 + i, item);
  }

  PyObject *result = Koskari_AsyncCall(
      KosBoundMethod_FUNC(self), bound_args, kwargs);
  Py_DECREF(bound_args);
  return result;
}


KosEvaluatedTypeObject KosBoundMethodType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib.BoundMethod",
    .tp_basicsize = sizeof(KosBoundMethod),
    .tp_dealloc = boundmethod_dealloc,
    .tp_traverse = boundmethod_traverse,
    .tp_clear = boundmethod_clear,
    .tp_repr = boundmethod_repr,
    .tp_call = KosCallAdapt_CallAsync,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "Bound method wrapper. Implements KosCallable by prepending"
              " self to call arguments.",
    .tp_members = boundmethod_members,
    .tp_getset = boundmethod_getsets,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kos_vinstall_boundmethod,
    .kc_pycall = boundmethod_pycall,
    .kc_pyvectorcall = NULL,
  },
};


PyObject *KosBoundMethod_New(PyObject *func, PyObject *self) {

  if (! KosFunction_CheckExact(func) && ! KosDemand_Check(func)) {
    PyErr_Format(PyExc_TypeError,
                 "KosBoundMethod requires Function or Demand, got %R",
                 func);
    return NULL;
  }
  if (! self) {
    PyErr_SetString(PyExc_TypeError, "KosBoundMethod requires non-NULL self");
    return NULL;
  }
  PyObject *bm = PyType_GenericNew(
      KOS_EVALUATED_PYTYPE(KosBoundMethodType), NULL, NULL);
  if (! bm)
    return NULL;
  Py_INCREF(func);
  KosBoundMethod_FUNC(bm) = func;
  Py_INCREF(self);
  KosBoundMethod_SELF(bm) = self;
  return bm;
}


/* === KosFunction: callable (code, formals, template_ns) -> coroutine on __call__ === */


/**
   Return a new reference to Code.extras["doc"] if that value is unicode.

   :param code: KosCode instance
   :return: new str ref, or NULL if missing or invalid
*/
static PyObject *
doc_from_code(PyObject *code) {

  PyObject *extras;
  PyObject *raw;

  if (! KosCode_Check(code))
    return NULL;
  extras = KosCode_EXTRAS(code);
  if (! extras || ! PyDict_Check(extras))
    return NULL;
  raw = PyDict_GetItemString(extras, "doc");
  if (! raw)
    return NULL;
  if (! PyUnicode_Check(raw))
    return NULL;
  Py_INCREF(raw);
  return raw;
}


/**
   Attach a creation-time annotations dict to a function, computing the
   forward-reference resolution flag. A NULL or None annotations value
   is a no-op. Increfs the dict; the caller keeps its own reference.
*/
static int
kos_function_attach_annotations(KosFunction *fn, PyObject *annotations) {

  if (! annotations || annotations == Py_None)
    return 0;

  if (! PyDict_Check(annotations)) {
    PyErr_Format(PyExc_TypeError, "Function annotations must be dict,"
                 " not %R", annotations);
    return -1;
  }

  Py_INCREF(annotations);
  Py_XSETREF(fn->creation_annotations, annotations);

  PyObject *key = NULL;
  PyObject *val = NULL;
  Py_ssize_t pos = 0;
  while (PyDict_Next(annotations, &pos, &key, &val)) {
    if (PyUnicode_Check(val)) {
      fn->annotations_need_resolve = 1;
      break;
    }
  }

  return 0;
}


static int function_init(
    KosFunction *self,
    PyObject *args,
    PyObject *kwargs) {

  PyObject *code = NULL;
  PyObject *base_ns = NULL;
  PyObject *formals_spec = NULL;
  PyObject *formals = NULL;
  PyObject *annotations = NULL;

  (void) kwargs;
  if (! PyArg_ParseTuple(args, "OOO|O:Function", &code, &base_ns,
                         &formals_spec, &annotations))
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

  PyObject *template = func_build_template_ns(code, formals, base_ns);
  if (! template)
    return -1;
  KosFunction_BASE_NS(self) = template;

  /* the function captures its defining namespace chain; reveal it so a
     closure stored back into scope forms a collectable cycle */
  KosNamespace_TrackChain(template);

  self->vectorcall = KosCallAdapt_VectorAsync;
  self->inspect_signature = NULL;
  self->creation_annotations = NULL;
  self->annotations_cache = NULL;
  self->annotations_need_resolve = 0;
  self->bind_self_state = 0;
  self->simple_arity1 = function_compute_simple_arity(code, formals) + 1;
  self->cached_frame = NULL;

  if (kos_function_attach_annotations(self, annotations) < 0)
    return -1;

  KosFunction_DOC(self) = doc_from_code(code);

  return 0;
}


/** Tear down the parked frame, if any; the parked node's fn is always
    NULL, so this cannot re-enter the function. */
static void func_drop_cached_frame(KosFunction *fn) {
  KosDynFrame *frame = fn->cached_frame;
  if (frame) {
    fn->cached_frame = NULL;
    kos_dynnode_free((KosDynNode *) frame);
  }
}


static int function_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  if (((KosFunction *) self)->cached_frame) {
    KosDynFrame *cached = ((KosFunction *) self)->cached_frame;
    int r = kos_dynnode_traverse((KosDynNode *) cached, visit, arg);
    if (r != 0)
      return r;
    /* the parked namespace is GC-untracked (that is a park condition);
       kos_dynnode_traverse reports its contents through the frame node,
       so a cycle involving a parked frame stays collectable */
  }
  Py_VISIT(KosFunction_NAME(self));
  Py_VISIT(KosFunction_CODE(self));
  Py_VISIT(KosFunction_BASE_NS(self));
  Py_VISIT(KosFunction_FORMALS(self));
  Py_VISIT(KosFunction_DOC(self));
  Py_VISIT(((KosFunction *) self)->inspect_signature);
  Py_VISIT(KosFunction_DICT(self));
  Py_VISIT(((KosFunction *) self)->creation_annotations);
  Py_VISIT(((KosFunction *) self)->annotations_cache);
  Py_VISIT(((KosFunction *) self)->name_override);
  Py_VISIT(((KosFunction *) self)->qualname_override);
  Py_VISIT(((KosFunction *) self)->module_override);
  return 0;
}


static int function_clear(PyObject *self) {

  func_drop_cached_frame((KosFunction *) self);
  Py_CLEAR(KosFunction_DOC(self));
  Py_CLEAR(((KosFunction *) self)->inspect_signature);
  Py_CLEAR(KosFunction_DICT(self));
  Py_CLEAR(((KosFunction *) self)->creation_annotations);
  Py_CLEAR(((KosFunction *) self)->annotations_cache);
  Py_CLEAR(((KosFunction *) self)->name_override);
  Py_CLEAR(((KosFunction *) self)->qualname_override);
  Py_CLEAR(((KosFunction *) self)->module_override);
  return 0;
}


static void function_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);
  func_drop_cached_frame((KosFunction *) self);
  Py_CLEAR(KosFunction_NAME(self));
  Py_CLEAR(KosFunction_CODE(self));
  Py_CLEAR(KosFunction_BASE_NS(self));
  Py_CLEAR(KosFunction_FORMALS(self));
  Py_CLEAR(KosFunction_DOC(self));
  Py_CLEAR(((KosFunction *) self)->inspect_signature);
  Py_CLEAR(KosFunction_DICT(self));
  Py_CLEAR(((KosFunction *) self)->creation_annotations);
  Py_CLEAR(((KosFunction *) self)->annotations_cache);
  Py_CLEAR(((KosFunction *) self)->name_override);
  Py_CLEAR(((KosFunction *) self)->qualname_override);
  Py_CLEAR(((KosFunction *) self)->module_override);
  Py_TYPE(self)->tp_free(self);
}


/**
   Build the template namespace for a function at creation time.  Returns
   namespace with formals slots and closure refs; parent points to base_ns.
*/
static PyObject *func_build_template_ns(PyObject *code, PyObject *formals,
                                        PyObject *parent_ns) {

  PyObject *base_nspec = KosCode_NSPEC(code);
  PyObject *nspec = KosFormals_NamespaceSpec(formals, base_nspec);
  if (! nspec)
    return NULL;
  PyObject *template = KosNamespace_FromSpec(nspec, parent_ns);
  Py_DECREF(nspec);
  return template;
}


/**
   Obtain the call namespace for a function: copy the template (stored in
   base_ns), apply formals and name in caller.  Returns new reference or
   NULL on error.
*/
static PyObject *func_obtain_call_namespace(KosFunction *fn) {
  return KosNamespace_Copy(KosFunction_BASE_NS(fn));
}


/** True when the formals field is absent or an empty container. */
static int formals_field_empty(PyObject *o) {
  if (! o || o == Py_None)
    return 1;
  if (PyTuple_Check(o))
    return PyTuple_GET_SIZE(o) == 0;
  if (PyDict_Check(o))
    return PyDict_GET_SIZE(o) == 0;
  return 0;
}


/**
  Classify a function for the simple-call fast path: positionally
  fillable runs only (pos and kwd; no pos-only / kwd-only / star /
  starstar / by-refs).  Slot i of the call namespace is positional i,
  so a FULL-supply call binds by index -- defaults may exist but are
  never consulted at full supply, which is the only shape the fast
  path accepts.  Yielding code classifies too: the generator gate in
  the call core peels those calls off before any install arm, and
  their namespaces get the same simple bind on the construction path.

  :return: the exact full-supply arity, or -1 when not simple
*/
static Py_ssize_t function_compute_simple_arity(
    PyObject *code,
    PyObject *formals) {

  Py_ssize_t arity = 0;

  if (! code || ! formals)
    return -1;

  if (! formals_field_empty(KosFormals_POS_ONLY(formals))
      || ! formals_field_empty(KosFormals_KWD_ONLY(formals))
      || KosFormals_STAR(formals)
      || KosFormals_STARSTAR(formals)
      || ! formals_field_empty(KosFormals_BY_REFS(formals)))
    return -1;

  {
    PyObject *pos = KosFormals_POS(formals);
    PyObject *kwd = KosFormals_KWD(formals);

    if (pos && pos != Py_None) {
      if (! PyTuple_Check(pos))
        return -1;
      arity += PyTuple_GET_SIZE(pos);
    }
    if (kwd && kwd != Py_None) {
      if (! PyTuple_Check(kwd))
        return -1;
      arity += PyTuple_GET_SIZE(kwd);
    }
  }
  return arity;
}


/**
  Stores self-reference in frame_ns if the function's name is listed in the
  Code's local_syms.

  The membership test is static per function (name and code are fixed after
  construction), so the verdict is computed once on the first call and
  cached in bind_self_state; every later call is a flag test.

  :param fn: KosFunction instance
  :param frame_ns: KosNamespace instance
  :param self: self-reference to bind :return: 0 if self-bind not applied, 1 if applied, -1 on error
*/
static int kos_function_bind_self_if_local(
    KosFunction *fn,
    PyObject *frame_ns,
    PyObject *self) {

  if (likely(fn->bind_self_state == 2))
    return 0;

  if (unlikely(fn->bind_self_state == 0)) {
    PyObject *name = KosFunction_NAME(fn);
    PyObject *code;
    PyObject *nspec;
    PyObject *locals;
    int bind = 0;

    code = name ? KosFunction_CODE(fn) : NULL;
    nspec = code ? KosCode_NSPEC(code) : NULL;
    locals = nspec ? KosNamespaceSpec_LOCAL_SYMS(nspec) : NULL;

    if (locals && locals != Py_None) {
      bind = PySequence_Contains(locals, name);
      if (bind < 0)
        return -1;  /* leave unresolved; retried on the next call */
    }

    fn->bind_self_state = bind ? 1 : 2;
    if (! bind)
      return 0;
  }

  return KosNamespace_Store(frame_ns, KosFunction_NAME(fn), self);
}


PyObject *KosFunction_Call(PyObject *self,
                           PyObject *args, PyObject *kwds) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  /* No keywords: ride the vector entry and its simple-bind arm. */
  if (! kwds) {
    PyObject *const *argv =
        args ? &PyTuple_GET_ITEM(args, 0) : NULL;
    Py_ssize_t nargs = args ? PyTuple_GET_SIZE(args) : 0;
    return KosFunction_VectorCall(self, argv, (size_t) nargs, NULL);
  }

  frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    goto fail;

  if (KosFormals_ApplyFast(KosFunction_FORMALS(fn), frame_ns, args, kwds) < 0)
    goto fail;

  if (kos_function_bind_self_if_local(fn, frame_ns, self) < 0)
    goto fail;

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

  if (KosFormals_ApplyFast(KosFunction_FORMALS(fn), frame_ns, args, kwds) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  if (kos_function_bind_self_if_local(fn, frame_ns, self) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  return frame_ns;
}


PyObject *KosFunction_PrepareCallNamespaceFromVector(
    PyObject *self,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    return NULL;

  if (KosFormals_VectorApplyFast(KosFunction_FORMALS(fn), frame_ns,
                                 args, (size_t) nargs, kwnames) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  if (kos_function_bind_self_if_local(fn, frame_ns, self) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  return frame_ns;
}


PyObject *KosFunction_SimpleCallNamespaceFromVector(
    PyObject *self,
    PyObject *const *args,
    Py_ssize_t nargs) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = KosNamespace_Copy(KosFunction_BASE_NS(fn));
  if (unlikely(! frame_ns))
    return NULL;

  /* slot i is positional i (classification guarantee) */
  for (Py_ssize_t i = 0; i < nargs; i++) {
    if (unlikely(kos_ns_fast_store(frame_ns, (size_t) i, args[i]) < 0)) {
      Py_DECREF(frame_ns);
      return NULL;
    }
  }

  if (unlikely(fn->bind_self_state != 2)
      && kos_function_bind_self_if_local(fn, frame_ns, self) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  return frame_ns;
}


PyObject *KosFunction_SimpleCallNamespaceStealBlock(
    PyObject *self,
    KosSegment *block,
    Py_ssize_t nargs) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = KosNamespace_Copy(KosFunction_BASE_NS(fn));
  if (unlikely(! frame_ns))
    return NULL;

  /* caller guarantees bind_self_state == 2, so nothing after the steal
     can fail: a NULL return always means the arg span is still intact
     on the block */
  if (unlikely(KosFormals_SimpleStealApplyBlock(frame_ns, block, nargs) < 0)) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  return frame_ns;
}


PyObject *KosFunction_SimpleCallNamespaceStealSelfBlock(
    PyObject *self,
    PyObject *recv,
    KosSegment *block,
    Py_ssize_t nargs) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = KosNamespace_Copy(KosFunction_BASE_NS(fn));
  if (unlikely(! frame_ns))
    return NULL;

  /* caller guarantees bind_self_state == 2, so nothing after the steal
     can fail: a NULL return always means the arg span is still intact
     on the block */
  if (unlikely(KosFormals_SimpleStealApplySelfBlock(
          frame_ns, recv, block, nargs) < 0)) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  return frame_ns;
}


PyObject *KosFunction_VectorCall(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  Py_ssize_t nargs = (Py_ssize_t) PyVectorcall_NARGS(nargsf);

  /* Simple call: bind by slot index, skipping the general formals
     machine -- the same classification the in-eval call core uses. */
  if (! kwnames
      && nargs + 1 == KosFunction_SIMPLE_ARITY1(self)
      && KosFunction_BIND_SELF_STATE(self) == 2) {
    frame_ns = KosFunction_SimpleCallNamespaceFromVector(self, args, nargs);
    if (! frame_ns)
      return NULL;
    coro = KosCoroutine_NewFromCode(KosFunction_CODE(fn), frame_ns);
    Py_DECREF(frame_ns);
    return coro;
  }

  frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    goto fail;

  if (KosFormals_VectorApplyFast(KosFunction_FORMALS(fn), frame_ns,
                                 args, (size_t) nargs, kwnames) < 0)
    goto fail;

  if (kos_function_bind_self_if_local(fn, frame_ns, self) < 0)
    goto fail;

  coro = KosCoroutine_NewFromCode(KosFunction_CODE(fn), frame_ns);
  Py_DECREF(frame_ns);
  return coro;

 fail:
  Py_XDECREF(frame_ns);
  return NULL;
}


PyObject *KosFunction_GetCode(PyObject *self) {
  Py_INCREF(KosFunction_CODE(self));
  return KosFunction_CODE(self);
}


PyObject *KosFunction_CallNoArgs(PyObject *self) {
  return KosFunction_Call(self, NULL, NULL);
}


static PyObject *function_descr_get(
    PyObject *self,
    PyObject *obj,
    PyObject *owner) {

  if (obj == NULL) {
    Py_INCREF(self);
    return self;
  }
  return KosBoundMethod_New(self, obj);
}


static PyObject *function_repr(PyObject *self) {
  KosFunction *fn = (KosFunction *) self;
  PyObject *name;

  /* display follows a __name__ override (wraps); self-binding does not */
  name = fn->name_override ? fn->name_override : KosFunction_NAME(fn);
  if (name == NULL)
    name = KosCode_NAME(KosFunction_CODE(fn));
  if (name != NULL)
    return PyUnicode_FromFormat("<koskari function %R at %p>", name, self);
  return PyUnicode_FromFormat("<koskari function <lambda> at %p>", self);
}


static PyMemberDef function_members[] = {
  { "code", T_OBJECT, offsetof(KosFunction, code), READONLY, NULL },
  { "base_ns", T_OBJECT, offsetof(KosFunction, base_ns), READONLY, NULL },
  { "formals", T_OBJECT, offsetof(KosFunction, formals), READONLY, NULL },
  { "name", T_OBJECT, offsetof(KosFunction, name), READONLY, NULL },
  { "__doc__", T_OBJECT, offsetof(KosFunction, doc), 0, NULL },
  { NULL, 0, 0, 0, NULL },
};


KosEvaluatedTypeObject KosFunctionType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.function",
  .tp_basicsize = sizeof(KosFunction),
  .tp_dealloc = function_dealloc,
  .tp_repr = function_repr,
  .tp_descr_get = function_descr_get,
  .tp_call = KosCallAdapt_CallAsync,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL
      | Py_TPFLAGS_METHOD_DESCRIPTOR | Py_TPFLAGS_HAVE_GC,
  .tp_vectorcall_offset = KosFunction_VECTORCALL_OFFSET,
  .tp_dictoffset = offsetof(KosFunction, dict),
  .tp_doc = "Callable(Code, base_namespace, FormalsSpec). Calling with "
             "(*args, **kwargs) binds formals and returns a Coroutine.",
  .tp_traverse = function_traverse,
  .tp_clear = function_clear,
  .tp_init = (initproc) function_init,
  .tp_new = PyType_GenericNew,
  .tp_members = function_members,
  .tp_getset = function_getsets,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kos_vinstall_function,
    .kc_pycall = KosFunction_Call,
    .kc_pyvectorcall = KosFunction_VectorCall,
  },
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

  PyObject *self = PyType_GenericNew(KOS_EVALUATED_PYTYPE(KosExpressionType), NULL, NULL);
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

  PyObject *self = PyType_GenericNew(KOS_EVALUATED_PYTYPE(KosFunctionType), NULL, NULL);
  if (! self)
    return NULL;

  Py_INCREF(code);
  KosFunction_CODE(self) = code;

  Py_INCREF(formals);
  KosFunction_FORMALS(self) = formals;

  PyObject *template = func_build_template_ns(code, formals, parent_ns);
  if (! template) {
    Py_DECREF(self);
    return NULL;
  }
  KosFunction_BASE_NS(self) = template;
  KosNamespace_TrackChain(template);

  KosFunction_NAME(self) = NULL;
  ((KosFunction *) self)->vectorcall = KosCallAdapt_VectorAsync;
  ((KosFunction *) self)->inspect_signature = NULL;
  ((KosFunction *) self)->creation_annotations = NULL;
  ((KosFunction *) self)->annotations_cache = NULL;
  ((KosFunction *) self)->annotations_need_resolve = 0;
  ((KosFunction *) self)->bind_self_state = 0;
  ((KosFunction *) self)->simple_arity1 =
      function_compute_simple_arity(code, formals) + 1;

  KosFunction_DOC(self) = doc_from_code(code);

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


PyObject *KosFunction_NewWithAnnotations(
    PyObject *func_spec,
    PyObject *kw_defaults,
    PyObject *parent_namespace,
    PyObject *annotations) {

  PyObject *fn = KosFunction_New(func_spec, kw_defaults, parent_namespace);
  if (! fn)
    return NULL;

  if (kos_function_attach_annotations((KosFunction *) fn, annotations) < 0) {
    Py_DECREF(fn);
    return NULL;
  }

  return fn;
}


/* === Unit lifecycle === */


int koskari_func_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosExpressionType)) ||
      PyType_Ready(&KosFunctionSpecType) ||
      PyType_Ready(KOS_EVALUATED_PYTYPE(KosBoundMethodType)) ||
      PyType_Ready(KOS_EVALUATED_PYTYPE(KosFunctionType)))
    return -1;

  if (PyDict_SetItemString(dict, "Expression",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosExpressionType)))
    return -1;

  if (PyDict_SetItemString(dict, "FunctionSpec",
                           (PyObject *) &KosFunctionSpecType))
    return -1;

  if (PyDict_SetItemString(dict, "Function",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosFunctionType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(dict, "function",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosFunctionType)))
    return -1;

  if (PyDict_SetItemString(dict, "BoundMethod",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosBoundMethodType)))
    return -1;

  return 0;
}


int koskari_func_clear(PyObject *mod) {

  (void) mod;

  Py_CLEAR(kos_inspect_module);
  Py_CLEAR(kos_inspect_Parameter);
  Py_CLEAR(kos_inspect_Signature);
  Py_CLEAR(kos_inspect_Parameter_empty);

  return 0;
}


// The end.
