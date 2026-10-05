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
  koskari/lib/func/func_inspect.c

  inspect.Signature interop and annotation realization for KosFunction
  and KosBoundMethod: the cached inspect module handles, signature
  construction from a formals spec, and forward-reference resolution of
  annotations. The accessors and getset tables that expose these stay
  with their type objects in func.c. Included into func.c as a segment
  at the point the code sat.

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


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


/**
   Resolve the inspect module, its Parameter and Signature classes,
   Parameter.empty, and the interned "return" key, committing all five
   together so the guard is only ever true for a complete set. On any
   failure nothing is stored and the next call starts over.

   :return: 0, or -1 with an exception set
*/
static int
kos_inspect_load(void) {

  PyObject *str_return;
  PyObject *module;
  PyObject *parameter = NULL;
  PyObject *signature = NULL;
  PyObject *empty = NULL;

  if (kos_inspect_Signature)
    return 0;

  str_return = PyUnicode_InternFromString("return");
  if (! str_return)
    return -1;

  module = PyImport_ImportModule("inspect");
  if (! module)
    goto fail;

  parameter = PyObject_GetAttrString(module, "Parameter");
  if (! parameter)
    goto fail;
  signature = PyObject_GetAttrString(module, "Signature");
  if (! signature)
    goto fail;
  empty = PyObject_GetAttrString(parameter, "empty");
  if (! empty)
    goto fail;

  kos_str_return = str_return;
  kos_inspect_module = module;
  kos_inspect_Parameter = parameter;
  kos_inspect_Signature = signature;
  kos_inspect_Parameter_empty = empty;
  return 0;

fail:
  Py_XDECREF(signature);
  Py_XDECREF(parameter);
  Py_XDECREF(module);
  Py_DECREF(str_return);
  return -1;
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
      PyObject *bound = KosNamespace_BorrowLooseRef(ns, name);
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

  /* resolution runs Python, which may have read __annotations__ and
     filled the cache under us: the first store wins */
  if (fn->annotations_cache) {
    Py_DECREF(out);
    return fn->annotations_cache;
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


// The end.
