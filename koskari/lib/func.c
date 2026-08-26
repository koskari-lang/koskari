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


static int kos_expression_wants_frame(PyObject *callable) {

  KosCode *code = (KosCode *) KosExpression_CODE(callable);
  return (code && KosCode_YIELDS(code)) ? 0 : 1;
}


static int kos_function_wants_frame(PyObject *callable) {

  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  return (code && KosCode_YIELDS(code)) ? 0 : 1;
}


static PyObject *boundmethod_resolve_function(PyObject *func);

static Py_ssize_t function_compute_simple_arity(
        PyObject *code,
        PyObject *formals);


static int kos_boundmethod_wants_frame(PyObject *callable) {

  PyObject *func = KosBoundMethod_FUNC(callable);

  if (! KosCallable_Check(func))
    return 1;

  return KosCallable_WantsFrame(func);
}


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


static PyMemberDef expression_members[] = {
  { "code", T_OBJECT, offsetof(KosExpression, code), READONLY, NULL },
  { "ns", T_OBJECT, offsetof(KosExpression, ns), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


/* === KosExpression install slots === */


/** Zero-arg install shared by the tuple and vector Expression slots. */
static KosOpResult kos_expression_install_core(
    KosEvalState *es,
    PyObject *callable) {

  KosCode *code = (KosCode *) KosExpression_CODE(callable);
  if (code && KosCode_YIELDS(code)) {
    PyObject *ns = KosExpression_NS(callable);
    Py_INCREF(ns);  // NewFromCodeAndNs steals a reference
    PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, ns);
    if (! agen)
      return EVAL_RAISE();
    return KosEvalState_StackPUSH(es, agen) < 0 ? EVAL_RAISE() : EVAL_NORMAL();
  }

  KosDynFrame *frame = KosEvalState_FRAME(es);
  PyObject *ns = KosExpression_NS(callable);
  Py_INCREF(ns);
  return KosEvalState_SetFrameCodeAndNamespace(es, frame, code, ns) < 0
      ? EVAL_RAISE() : EVAL_NORMAL();
}


static KosOpResult kos_install_expression(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  (void) tail_call;

  if (args_tuple && PyTuple_GET_SIZE(args_tuple) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
    return EVAL_RAISE();
  }
  if (kwds_dict && PyDict_Size(kwds_dict) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return EVAL_RAISE();
  }

  return kos_expression_install_core(es, callable);
}


static KosOpResult kos_vinstall_expression(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  (void) args;
  (void) tail_call;

  if (nargs != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
    return EVAL_RAISE();
  }
  if (kwnames && PyTuple_GET_SIZE(kwnames) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return EVAL_RAISE();
  }

  return kos_expression_install_core(es, callable);
}


/* === KosFunction install slots === */


static KosOpResult kos_install_function(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  (void) tail_call;
  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  PyObject *frame_ns = KosFunction_PrepareCallNamespace(
      callable, args_tuple, kwds_dict);
  if (! frame_ns)
    return EVAL_RAISE();
  if (code && KosCode_YIELDS(code)) {
    PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, frame_ns);
    if (! agen)
      return EVAL_RAISE();
    /* Steal our owned reference onto the stack; StackPUSH consumes agen on
       both success and failure (no separate DECREF). */
    if (KosEvalState_StackPUSH(es, agen) < 0)
      return EVAL_RAISE();
    return EVAL_NORMAL();
  }
  KosDynFrame *frame = KosEvalState_FRAME(es);
  if (KosEvalState_SetFrameCodeAndNamespace(es, frame, code, frame_ns) < 0)
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


static KosOpResult kos_vinstall_function(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  (void) tail_call;
  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  PyObject *frame_ns = KosFunction_PrepareCallNamespaceFromVector(
      callable, args, nargs, kwnames);
  if (! frame_ns)
    return EVAL_RAISE();
  if (code && KosCode_YIELDS(code)) {
    PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, frame_ns);
    if (! agen)
      return EVAL_RAISE();
    /* Steal our owned reference onto the stack; StackPUSH consumes agen on
       both success and failure (no separate DECREF). */
    if (KosEvalState_StackPUSH(es, agen) < 0)
      return EVAL_RAISE();
    return EVAL_NORMAL();
  }
  KosDynFrame *frame = KosEvalState_FRAME(es);
  if (KosEvalState_SetFrameCodeAndNamespace(es, frame, code, frame_ns) < 0)
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


/**
   ki_pycall worker: an Expression takes no arguments; evaluate it.
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
  .tp_repr = (reprfunc) NULL,
  .tp_call = KosCallAdapt_CallAsync,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Callable(Code, namespace). Calling with () returns a Coroutine.",
  .tp_init = (initproc) expression_init,
  .tp_new = PyType_GenericNew,
  .tp_members = expression_members,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .ki_install = kos_install_expression,
    .ki_vinstall = kos_vinstall_expression,
    .ki_wants_frame = 1,
    .ki_wants_frame_fn = kos_expression_wants_frame,
    .ki_pycall = expression_pycall,
    .ki_pyvectorcall = NULL,
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


/* === inspect.Signature interop (KosFunction / KosBoundMethod) ===
   See design/archive/inspect-interop-kos-callables.md. KosFormals_ApplyFast
   consumes positionals in order pos, pos_only, kwd, *star, then kwargs
   phase for kwd_only and **starstar. If that ordering is invalid for
   inspect.Signature (e.g. POSITIONAL_ONLY after POSITIONAL_OR_KEYWORD),
   we retry with positional-only names emitted as POSITIONAL_OR_KEYWORD.
*/


static PyObject *kos_inspect_module = NULL;
static PyObject *kos_inspect_Parameter = NULL;
static PyObject *kos_inspect_Signature = NULL;
static PyObject *kos_inspect_Parameter_empty = NULL;
static PyObject *kos_str_return = NULL;


static int
kos_inspect_load(void) {

  if (kos_inspect_Signature)
    return 0;

  kos_str_return = PyUnicode_InternFromString("return");
  if (! kos_str_return)
    return -1;

  kos_inspect_module = PyImport_ImportModule("inspect");
  if (! kos_inspect_module)
    return -1;

  kos_inspect_Parameter = PyObject_GetAttrString(kos_inspect_module,
                                                 "Parameter");
  kos_inspect_Signature = PyObject_GetAttrString(kos_inspect_module,
                                                 "Signature");
  if (! kos_inspect_Parameter || ! kos_inspect_Signature)
    return -1;

  kos_inspect_Parameter_empty = PyObject_GetAttrString(kos_inspect_Parameter,
                                                       "empty");
  if (! kos_inspect_Parameter_empty)
    return -1;

  return 0;
}


static PyObject *
kos_formal_name_str(PyObject *sym) {

  if (PyUnicode_Check(sym)) {
    Py_INCREF(sym);
    return sym;
  }
  if (KosSymbol_Check(sym)) {
    PyObject *n = KosInternedAtom_NAME(sym);
    Py_INCREF(n);
    return n;
  }
  return PyObject_Str(sym);
}


static PyObject *
kos_formals_default_for(PyObject *defaults, PyObject *name_sym,
                        PyObject *name_str, PyObject *param_empty) {

  PyObject *val = NULL;

  if (! defaults)
    goto use_empty;

  val = PyDict_GetItemWithError(defaults, name_sym);
  if (! val && PyErr_Occurred())
    return NULL;
  if (! val && name_str != name_sym) {
    val = PyDict_GetItemWithError(defaults, name_str);
    if (! val && PyErr_Occurred())
      return NULL;
  }
  if (! val) {
 use_empty:
    Py_INCREF(param_empty);
    return param_empty;
  }
  Py_INCREF(val);
  return val;
}


/**
   Look up the realized annotation for a parameter name. Returns a new
   reference to the annotation value, or to param_empty when the name is
   not annotated. NULL on error.
*/
static PyObject *
kos_annotation_value_for(PyObject *annotations, PyObject *name_str,
                         PyObject *param_empty) {

  PyObject *val = NULL;

  if (annotations) {
    val = PyDict_GetItemWithError(annotations, name_str);
    if (! val && PyErr_Occurred())
      return NULL;
  }

  if (! val)
    val = param_empty;

  Py_INCREF(val);
  return val;
}


static int
kos_append_inspect_parameter(
    PyObject *param_list,
    PyObject *name_obj,
    int kind,
    PyObject *defaults,
    PyObject *annotations,
    PyObject *param_empty) {

  PyObject *name_str = kos_formal_name_str(name_obj);
  if (! name_str)
    return -1;

  PyObject *def = kos_formals_default_for(defaults, name_obj, name_str,
                                          param_empty);
  if (! def) {
    Py_DECREF(name_str);
    return -1;
  }

  PyObject *ann = kos_annotation_value_for(annotations, name_str,
                                           param_empty);
  if (! ann) {
    Py_DECREF(def);
    Py_DECREF(name_str);
    return -1;
  }

  PyObject *kind_obj = PyLong_FromLong(kind);
  if (! kind_obj) {
    Py_DECREF(ann);
    Py_DECREF(def);
    Py_DECREF(name_str);
    return -1;
  }

  PyObject *args = PyTuple_Pack(2, name_str, kind_obj);
  Py_DECREF(kind_obj);
  Py_DECREF(name_str);
  if (! args) {
    Py_DECREF(ann);
    Py_DECREF(def);
    return -1;
  }

  PyObject *kwargs = Py_BuildValue("{sO,sO}", "default", def, "annotation",
                                   ann);
  Py_DECREF(ann);
  Py_DECREF(def);
  if (! kwargs) {
    Py_DECREF(args);
    return -1;
  }

  PyObject *param = PyObject_Call(kos_inspect_Parameter, args, kwargs);
  Py_DECREF(args);
  Py_DECREF(kwargs);
  if (! param)
    return -1;

  if (PyList_Append(param_list, param) < 0) {
    Py_DECREF(param);
    return -1;
  }
  Py_DECREF(param);
  return 0;
}


static int
kos_append_tuple_params(
    PyObject *param_list,
    PyObject *tuple,
    int kind,
    PyObject *defaults,
    PyObject *annotations,
    PyObject *param_empty) {

  Py_ssize_t i;
  Py_ssize_t n;

  if (! tuple)
    return 0;

  n = PyTuple_GET_SIZE(tuple);
  for (i = 0; i < n; i++) {
    PyObject *name = PyTuple_GET_ITEM(tuple, i);
    if (kos_append_inspect_parameter(param_list, name, kind, defaults,
                                    annotations, param_empty) < 0)
      return -1;
  }
  return 0;
}


static int
kos_append_var_param(
    PyObject *param_list,
    PyObject *name_unicode,
    int kind,
    PyObject *annotations,
    PyObject *param_empty) {

  PyObject *ann;
  PyObject *kind_obj;
  PyObject *args;
  PyObject *kwargs;
  PyObject *param;

  if (! name_unicode)
    return 0;

  ann = kos_annotation_value_for(annotations, name_unicode, param_empty);
  if (! ann)
    return -1;

  Py_INCREF(name_unicode);

  kind_obj = PyLong_FromLong(kind);
  if (! kind_obj) {
    Py_DECREF(name_unicode);
    Py_DECREF(ann);
    return -1;
  }

  args = PyTuple_Pack(2, name_unicode, kind_obj);
  Py_DECREF(kind_obj);
  Py_DECREF(name_unicode);
  if (! args) {
    Py_DECREF(ann);
    return -1;
  }

  kwargs = Py_BuildValue("{sO,sO}", "default", param_empty, "annotation",
                         ann);
  Py_DECREF(ann);
  if (! kwargs) {
    Py_DECREF(args);
    return -1;
  }

  param = PyObject_Call(kos_inspect_Parameter, args, kwargs);
  Py_DECREF(args);
  Py_DECREF(kwargs);
  if (! param)
    return -1;

  if (PyList_Append(param_list, param) < 0) {
    Py_DECREF(param);
    return -1;
  }
  Py_DECREF(param);
  return 0;
}


enum {
  KOS_PARAM_PO = 0,
  KOS_PARAM_POK = 1,
  KOS_PARAM_VAR_POS = 2,
  KOS_PARAM_KO = 3,
  KOS_PARAM_VAR_KW = 4
};


static PyObject *
kos_formals_build_param_list(
    PyObject *formals,
    int pos_only_as_pok,
    PyObject *annotations) {

  PyObject *param_list = NULL;
  PyObject *defaults = NULL;
  int po_kind;

  if (! KosFormals_Check(formals)) {
    PyErr_SetString(PyExc_TypeError, "expected Formals");
    return NULL;
  }

  if (kos_inspect_load() < 0)
    return NULL;

  param_list = PyList_New(0);
  if (! param_list)
    return NULL;

  defaults = KosFormals_DEFAULTS(formals);
  po_kind = pos_only_as_pok ? KOS_PARAM_POK : KOS_PARAM_PO;

  if (kos_append_tuple_params(param_list, KosFormals_POS(formals),
                              KOS_PARAM_POK, defaults, annotations,
                              kos_inspect_Parameter_empty) < 0)
    goto fail;

  if (kos_append_tuple_params(param_list, KosFormals_POS_ONLY(formals),
                              po_kind, defaults, annotations,
                              kos_inspect_Parameter_empty) < 0)
    goto fail;

  if (kos_append_tuple_params(param_list, KosFormals_KWD(formals),
                              KOS_PARAM_POK, defaults, annotations,
                              kos_inspect_Parameter_empty) < 0)
    goto fail;

  if (kos_append_var_param(param_list, KosFormals_STAR(formals),
                           KOS_PARAM_VAR_POS, annotations,
                           kos_inspect_Parameter_empty) < 0)
    goto fail;

  if (kos_append_tuple_params(param_list, KosFormals_KWD_ONLY(formals),
                              KOS_PARAM_KO, defaults, annotations,
                              kos_inspect_Parameter_empty) < 0)
    goto fail;

  if (kos_append_var_param(param_list, KosFormals_STARSTAR(formals),
                           KOS_PARAM_VAR_KW, annotations,
                           kos_inspect_Parameter_empty) < 0)
    goto fail;

  return param_list;

 fail:
  Py_DECREF(param_list);
  return NULL;
}


/**
   Call inspect.Signature(param_list), with return_annotation when one
   is supplied. New reference or NULL on error.
*/
static PyObject *
kos_call_signature(PyObject *param_list, PyObject *return_annotation) {

  PyObject *args = PyTuple_Pack(1, param_list);
  if (! args)
    return NULL;

  PyObject *kwargs = NULL;
  if (return_annotation) {
    kwargs = Py_BuildValue("{sO}", "return_annotation", return_annotation);
    if (! kwargs) {
      Py_DECREF(args);
      return NULL;
    }
  }

  PyObject *sig = PyObject_Call(kos_inspect_Signature, args, kwargs);
  Py_DECREF(args);
  Py_XDECREF(kwargs);
  return sig;
}


static PyObject *
kos_formals_to_signature(PyObject *formals, PyObject *annotations) {

  PyObject *param_list = NULL;
  PyObject *sig = NULL;
  PyObject *ret_ann = NULL;

  if (kos_inspect_load() < 0)
    return NULL;

  if (annotations) {
    ret_ann = PyDict_GetItemWithError(annotations, kos_str_return);
    if (! ret_ann && PyErr_Occurred())
      return NULL;
  }

  param_list = kos_formals_build_param_list(formals, 0, annotations);
  if (! param_list)
    return NULL;

  sig = kos_call_signature(param_list, ret_ann);
  Py_DECREF(param_list);
  if (! sig && PyErr_ExceptionMatches(PyExc_ValueError)) {
    PyErr_Clear();
    param_list = kos_formals_build_param_list(formals, 1, annotations);
    if (! param_list)
      return NULL;
    sig = kos_call_signature(param_list, ret_ann);
    Py_DECREF(param_list);
  }

  return sig;
}


static PyObject *
kos_signature_drop_first(PyObject *sig) {

  PyObject *params_map = NULL;
  PyObject *vals = NULL;
  PyObject *lst = NULL;
  PyObject *sig2 = NULL;
  Py_ssize_t n;

  params_map = PyObject_GetAttrString(sig, "parameters");
  if (! params_map)
    return NULL;

  {
    PyObject *vmeth = PyObject_GetAttrString(params_map, "values");
    Py_DECREF(params_map);
    if (! vmeth)
      return NULL;
    vals = PyObject_CallObject(vmeth, NULL);
    Py_DECREF(vmeth);
  }
  if (! vals)
    return NULL;

  lst = PySequence_List(vals);
  Py_DECREF(vals);
  if (! lst)
    return NULL;

  n = PyList_GET_SIZE(lst);

  /* rebuild via sig.replace(parameters=rest) so return_annotation
     carries across */
  {
    PyObject *rest = PyList_GetSlice(lst, (n > 0) ? 1 : 0, n);
    Py_DECREF(lst);
    lst = NULL;
    if (! rest)
      return NULL;

    PyObject *meth = PyObject_GetAttrString(sig, "replace");
    if (! meth) {
      Py_DECREF(rest);
      return NULL;
    }
    PyObject *args = PyTuple_New(0);
    PyObject *kwargs = Py_BuildValue("{sO}", "parameters", rest);
    Py_DECREF(rest);
    if (! args || ! kwargs) {
      Py_XDECREF(args);
      Py_XDECREF(kwargs);
      Py_DECREF(meth);
      return NULL;
    }
    sig2 = PyObject_Call(meth, args, kwargs);
    Py_DECREF(args);
    Py_DECREF(kwargs);
    Py_DECREF(meth);
  }

  return sig2;
}


/* === Annotations realization === */


/**
   Walk the namespace chain for the first segment of an annotation
   forward reference. Declared-but-unbound slots (formals and locals in
   a template namespace) are skipped so lookup continues up the chain;
   a module namespace consults its binds, then its builtins dict.

   New reference, or NULL. On a plain miss no error is set; a set error
   indicates a genuine failure.
*/
static PyObject *
kos_annotation_ns_lookup(PyObject *ns, PyObject *name) {

  while (ns) {
    if (KosModuleNamespace_CheckExact(ns)) {
      PyObject *bound = PyDict_GetItemWithError(KosNamespace_BINDS(ns),
                                                name);
      if (! bound && PyErr_Occurred())
        return NULL;
      if (bound) {
        PyObject *val = KosNamespace_Deref(ns, name);
        if (val)
          return val;
        PyErr_Clear();
      }

      PyObject *blt = KosModuleNamespace_BUILTINS(ns);
      if (blt) {
        PyObject *val = PyDict_GetItemWithError(blt, name);
        if (! val && PyErr_Occurred())
          return NULL;
        if (val) {
          Py_INCREF(val);
          return val;
        }
      }

    } else {
      PyObject *bound = KosNamespace_LOOSE_REF(ns, name);
      if (! bound && PyErr_Occurred())
        return NULL;
      if (bound) {
        PyObject *val = KosNamespace_Deref(ns, name);
        if (val)
          return val;
        PyErr_Clear();
      }
    }

    ns = KosNamespace_PARENT(ns);
  }

  return NULL;
}


/**
   Resolve one str annotation value as a dotted forward reference
   against the namespace chain starting at ns. The first segment is a
   namespace lookup; remaining segments are getattr chained.

   New reference, or NULL. On resolution failure (lookup miss,
   AttributeError) no error is set and the caller keeps the string; a
   set error indicates a genuine failure.
*/
static PyObject *
kos_annotation_resolve_str(PyObject *ns, PyObject *sval) {

  PyObject *sep = PyUnicode_FromString(".");
  if (! sep)
    return NULL;

  PyObject *parts = PyUnicode_Split(sval, sep, -1);
  Py_DECREF(sep);
  if (! parts)
    return NULL;

  PyObject *val = kos_annotation_ns_lookup(ns, PyList_GET_ITEM(parts, 0));
  if (! val) {
    Py_DECREF(parts);
    return NULL;
  }

  Py_ssize_t n = PyList_GET_SIZE(parts);
  for (Py_ssize_t i = 1; i < n; i++) {
    PyObject *next = PyObject_GetAttr(val, PyList_GET_ITEM(parts, i));
    Py_DECREF(val);
    if (! next) {
      if (PyErr_ExceptionMatches(PyExc_AttributeError))
        PyErr_Clear();
      Py_DECREF(parts);
      return NULL;
    }
    val = next;
  }

  Py_DECREF(parts);
  return val;
}


/**
   Realize the function's __annotations__ dict from its creation-time
   annotations, resolving str forward references against the defining
   namespace. The result is cached on first access; a str that fails to
   resolve stays a string and is never retried.

   Borrowed reference to the cache, or NULL on error.
*/
static PyObject *
kos_function_realize_annotations(KosFunction *fn) {

  if (fn->annotations_cache)
    return fn->annotations_cache;

  PyObject *creation = fn->creation_annotations;
  PyObject *out;

  if (! creation) {
    out = PyDict_New();
    if (! out)
      return NULL;
    fn->annotations_cache = out;
    return out;
  }

  out = PyDict_Copy(creation);
  if (! out)
    return NULL;

  if (fn->annotations_need_resolve) {
    PyObject *ns = KosFunction_BASE_NS((PyObject *) fn);

    /* base_ns is the template and carries declared-but-unbound slots
       for the formals and code locals; annotation names live in the
       defining scope, which is the template's parent */
    if (ns && KosNamespace_PARENT(ns))
      ns = KosNamespace_PARENT(ns);

    PyObject *key = NULL;
    PyObject *val = NULL;
    Py_ssize_t pos = 0;
    while (PyDict_Next(out, &pos, &key, &val)) {
      if (! PyUnicode_Check(val))
        continue;
      PyObject *resolved = kos_annotation_resolve_str(ns, val);
      if (! resolved) {
        if (PyErr_Occurred()) {
          Py_DECREF(out);
          return NULL;
        }
        continue;
      }
      /* replacing the value of an existing key is safe mid-iteration */
      if (PyDict_SetItem(out, key, resolved) < 0) {
        Py_DECREF(resolved);
        Py_DECREF(out);
        return NULL;
      }
      Py_DECREF(resolved);
    }
  }

  fn->annotations_cache = out;
  return out;
}


static PyObject *
kos_function_compute_signature(PyObject *self) {

  PyObject *formals = KosFunction_FORMALS(self);

  PyObject *annotations = kos_function_realize_annotations(
    (KosFunction *) self);
  if (! annotations)
    return NULL;

  if (kos_inspect_load() < 0)
    return NULL;

  return kos_formals_to_signature(formals, annotations);
}


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


static KosOpResult kos_install_boundmethod(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  PyObject *func = KosBoundMethod_FUNC(callable);
  PyObject *self = KosBoundMethod_SELF(callable);
  Py_ssize_t n = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
  PyObject *bound_args = NULL;
  KosOpResult r;

  if (! KosCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError,
                    "KosBoundMethod eval requires a KosCallable func");
    return EVAL_RAISE();
  }

  bound_args = PyTuple_New(1 + n);
  if (! bound_args)
    return EVAL_RAISE();

  Py_INCREF(self);
  PyTuple_SET_ITEM(bound_args, 0, self);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *item = PyTuple_GET_ITEM(args_tuple, i);
    Py_INCREF(item);
    PyTuple_SET_ITEM(bound_args, 1 + i, item);
  }
  r = KosCallable_CallArgs(es, func, bound_args, kwds_dict, tail_call);
  Py_DECREF(bound_args);
  return r;
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
  r = KosCallable_Call(es, func, new_args, nargs + 1, kwnames, tail_call);
  PyMem_Del(new_args);
  return r;
}




static void boundmethod_dealloc(PyObject *self) {
  Py_CLEAR(KosBoundMethod_FUNC(self));
  Py_CLEAR(KosBoundMethod_SELF(self));
  Py_TYPE(self)->tp_free(self);
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
   ki_pycall worker: prepend self and follow the wrapped callable
   through the generic boundary entry.
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
    .tp_repr = boundmethod_repr,
    .tp_call = KosCallAdapt_CallAsync,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Bound method wrapper. Implements KosCallable by prepending"
              " self to call arguments.",
    .tp_members = boundmethod_members,
    .tp_getset = boundmethod_getsets,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .ki_install = kos_install_boundmethod,
    .ki_vinstall = kos_vinstall_boundmethod,
    .ki_wants_frame = 1,
    .ki_wants_frame_fn = kos_boundmethod_wants_frame,
    .ki_pycall = boundmethod_pycall,
    .ki_pyvectorcall = NULL,
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
    /* the parked namespace is GC-untracked (that is a park condition),
       so its references -- notably parent, into the tracked base-ns
       chain -- must be attributed through the function or cycles
       involving a parked frame become uncollectable */
    {
      PyObject *pns = KosDynFrame_NS(cached);
      if (pns) {
        r = Py_TYPE(pns)->tp_traverse(pns, visit, arg);
        if (r != 0)
          return r;
      }
    }
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
  starstar / by-refs) on non-yielding code.  Slot i of the call
  namespace is positional i, so a FULL-supply call binds by index --
  defaults may exist but are never consulted at full supply, which is
  the only shape the fast path accepts.

  :return: the exact full-supply arity, or -1 when not simple
*/
static Py_ssize_t function_compute_simple_arity(
    PyObject *code,
    PyObject *formals) {

  Py_ssize_t arity = 0;

  if (! code || ! formals || KosCode_YIELDS(code))
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


int KosFunction_TryParkFrame(PyObject *self, KosDynFrame *frame) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *ns;

  if (fn->cached_frame)
    return 0;

  /* The namespace must be provably private to the frame: a captured,
     revealed, or dynamically-extended namespace stays live-visible and
     the frame takes the ordinary teardown instead. */
  ns = KosDynFrame_NS(frame);
  if (! ns
      || ! KosNamespace_CheckExact(ns)
      || Py_REFCNT(ns) != 1
      || PyObject_GC_IsTracked(ns)
      || KosNamespace_BINDS(ns) != NULL
      || KosDynFrame_NATIVE_HANDLER(frame)
      || ! KosDynFrame_STACK(frame))
    return 0;

  /* Reset to the state a fresh install would produce: raw slots NULL
     (non-raw template refs retained), empty stack, normalized node
     base ready for kos_dyn_push_ready_frame. */
  KosNamespace_ClearRawFast(ns);
  KosStack_Clear(&KosDynFrame_STACK(frame));
  KosSegment_BACK(KosDynFrame_STACK(frame)) = NULL;
  KosDynFrame_BACK_FRAME(frame) = NULL;

  KosDynNode_PARENT(frame) = NULL;
  KosDynNode_FLAGS(frame) = KOS_DYNF_NONE;
  KosDynNode_CAPS(frame) = 0;
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_FLAGS(frame) = 0;

  fn->cached_frame = frame;
  return 1;
}


PyObject *KosFunction_VectorCall(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    goto fail;

  Py_ssize_t nargs = (Py_ssize_t) PyVectorcall_NARGS(nargsf);
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
  if (name != NULL)
    return PyUnicode_FromFormat("<Function %R>", name);
  name = KosCode_NAME(KosFunction_CODE(fn));
  if (name != NULL)
    return PyUnicode_FromFormat("<Function %R>", name);
  return PyUnicode_FromString("<Function <lambda>>");
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
  .tp_name = "koskari.lib.Function",
  .tp_basicsize = sizeof(KosFunction),
  .tp_dealloc = function_dealloc,
  .tp_repr = function_repr,
  .tp_descr_get = function_descr_get,
  .tp_call = KosCallAdapt_CallAsync,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL | Py_TPFLAGS_HAVE_GC,
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
    .ki_install = kos_install_function,
    .ki_vinstall = kos_vinstall_function,
    .ki_wants_frame = 1,
    .ki_wants_frame_fn = kos_function_wants_frame,
    .ki_pycall = KosFunction_Call,
    .ki_pyvectorcall = KosFunction_VectorCall,
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
