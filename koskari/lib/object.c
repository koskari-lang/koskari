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

   Functional class builder. kos_build_class demand-wraps the
   synchronous dunders in the class namespace (Demand for
   value-returning dunders, Procedure for __init__, IterFunction for a
   generator __iter__, staticmethod(Demand) for __new__,
   classmethod(Demand) for the class-construction hooks)
   and delegates construction to the
   winning metaclass -- plain type by default. CPython's own slot
   machinery then dispatches tp_repr, tp_richcompare, and the rest
   through the wrapped, synchronous callables; no custom metaclass is
   involved. The one post-construction touch is the kos_instance_call
   sentinel, installed as tp_call when __call__ resolves to a
   KosCallable so the evaluator can inline instance calls.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* === Interned names === */


static PyObject *str_call_name = NULL;
static PyObject *str_func_name = NULL;
static PyObject *str_class_name = NULL;
static PyObject *str_iter_name = NULL;
static PyObject *str_mro_entries = NULL;
static PyObject *str_orig_bases = NULL;


/**
   Dunders that cannot tolerate a coroutine escaping into a synchronous
   Python protocol, grouped by the wrapper each needs. Values under
   these names that are KosCallables get wrapped at class-build time;
   growing coverage is one string per list.

   - procedure dunders: run on the demand path with the result
     discarded (slot_tp_init rejects a non-None return).
   - static dunders: canonicalized to staticmethod(Demand); type_new
     staticmethod-wraps these itself but only for exact plain Python
     functions.
   - class dunders: canonicalized to classmethod(Demand); same
     PyFunction-only gate in type_new's auto-classmethod treatment.
   - __iter__ bound to a generator KosFunction: wrapped in IterFunction,
     which presents the generator to Python as a KosPyIter. Any other
     __iter__ is a demand dunder and must return an iterator itself.
   - demand dunders: every other synchronous object dunder, wrapped in
     Demand.

   Deliberately absent: __call__ (async semantics preserved; the
   evaluator inlines it via the kos_instance_call sentinel) and the
   async protocol dunders (__await__, __aiter__, __anext__, __aenter__,
   __aexit__).
*/


static const char *procedure_dunders[] = {
  "__init__",
  NULL,
};


static const char *static_dunders[] = {
  "__new__",
  NULL,
};


static const char *class_dunders[] = {
  "__init_subclass__",
  "__class_getitem__",
  "__subclasshook__",
  NULL,
};


static const char *demand_dunders[] = {
  "__repr__", "__str__", "__format__", "__bytes__",
  "__hash__", "__bool__",
  "__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__",
  "__getattr__", "__getattribute__", "__setattr__", "__delattr__",
  "__dir__",
  "__get__", "__set__", "__delete__", "__set_name__",
  "__len__", "__length_hint__",
  "__getitem__", "__setitem__", "__delitem__", "__missing__",
  "__iter__", "__next__", "__reversed__", "__contains__",
  "__add__", "__radd__", "__iadd__",
  "__sub__", "__rsub__", "__isub__",
  "__mul__", "__rmul__", "__imul__",
  "__matmul__", "__rmatmul__", "__imatmul__",
  "__truediv__", "__rtruediv__", "__itruediv__",
  "__floordiv__", "__rfloordiv__", "__ifloordiv__",
  "__mod__", "__rmod__", "__imod__",
  "__divmod__", "__rdivmod__",
  "__pow__", "__rpow__", "__ipow__",
  "__lshift__", "__rlshift__", "__ilshift__",
  "__rshift__", "__rrshift__", "__irshift__",
  "__and__", "__rand__", "__iand__",
  "__xor__", "__rxor__", "__ixor__",
  "__or__", "__ror__", "__ior__",
  "__neg__", "__pos__", "__abs__", "__invert__",
  "__complex__", "__int__", "__float__", "__index__",
  "__round__", "__trunc__", "__floor__", "__ceil__",
  "__enter__", "__exit__",
  "__reduce__", "__reduce_ex__",
  "__getstate__", "__setstate__",
  "__getnewargs__", "__getnewargs_ex__",
  "__copy__", "__deepcopy__", "__sizeof__",
  "__fspath__", "__del__",
  "__instancecheck__", "__subclasscheck__",
  NULL,
};


#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))

static PyObject *procedure_names[COUNT_OF(procedure_dunders)];
static PyObject *static_names[COUNT_OF(static_dunders)];
static PyObject *class_names[COUNT_OF(class_dunders)];
static PyObject *demand_names[COUNT_OF(demand_dunders)];


/* === Procedure === */


/*
  Procedure is a Demand whose result is discarded: calling it runs the
  wrapped callable on the demand path and returns None. It exists for
  __init__, where slot_tp_init raises TypeError on a non-None return
  while a Koskari function returns its body's value. Kept out of the
  module dict for now; construction, binding, idempotent re-wrap, and
  attribute forwarding are all inherited from Demand.
*/


/**
   The Python vectorcall entry: the Demand call over the vector, on the
   context this wrapper caches, result discarded. A method-descriptor
   call from CPython (slot_tp_init) arrives here unbound with self
   first in args.
*/
static PyObject *procedure_vectorcall(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  PyObject *result = KosDemand_CallVector(self, args, nargsf, kwnames);

  if (! result)
    return NULL;
  Py_DECREF(result);
  Py_RETURN_NONE;
}


static PyObject *procedure_repr(PyObject *self) {

  PyObject *wrapped = KosDemand_GetWrapped(self);

  if (! wrapped)
    return PyUnicode_FromString("<Procedure ?>");

  PyObject *name = PyObject_GetAttrString(wrapped, "__name__");
  if (! name) {
    PyErr_Clear();
    return PyUnicode_FromString("<Procedure <lambda>>");
  }

  PyObject *result = PyUnicode_FromFormat("<Procedure %R>", name);
  Py_DECREF(name);
  return result;
}


/**
   The resume on a Procedure's code-less frame: receives the wrapped
   callable's return and completes the frame with None in its place.
   A failed delivery propagates.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult procedure_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  (void) state;

  if (! value)
    return EVAL_RAISE();

  Py_DECREF(value);
  Py_INCREF(Py_None);
  return KosNativeFrame_ReturnToCache(ctx, Py_None, NULL);
}


KOS_RESUME_KIND(procedure_resume_kind, "procedure",
                procedure_resume, NULL, kos_resume_clone_shared);


/**
   Evaluator install: call the wrapped callable from a native frame
   carrying procedure_resume and marked as the demand root, so the
   call runs in this evaluator and cannot suspend; in tail position
   that frame is the caller's, reused. The resume completes the frame
   with None.
*/
static KosOpResult procedure_kos_vinstall(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  PyObject *wrapped = KosDemand_GetWrapped(callable);

  if (! wrapped) {
    PyErr_SetString(PyExc_TypeError, "Procedure object is not initialized");
    return EVAL_RAISE();
  }

  return KosContext_CallInto(
      ctx, wrapped, args, nargs, kwnames, &procedure_resume_kind, NULL,
      KOS_FRAME_DEMAND, tail_call);
}


static KosEvaluatedTypeObject KosProcedureCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.Procedure",
    .tp_basicsize = 0,  /* inherited from Demand by PyType_Ready */
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL
        | Py_TPFLAGS_METHOD_DESCRIPTOR,
    .tp_vectorcall_offset = KosDemand_VECTORCALL_OFFSET,
    .tp_doc = "A Demand with no result: runs its wrapped callable on the "
              "demand path and returns None",
    .tp_repr = procedure_repr,
    .tp_call = PyVectorcall_Call,
    .tp_base = KOS_EVALUATED_PYTYPE(KosDemandCallableType),
  },
  .kos_call = {
    .kc_vinstall = procedure_kos_vinstall,
    .kc_pycall = NULL,
    .kc_pyvectorcall = procedure_vectorcall,
  },
};


static PyObject *KosProcedure_New(PyObject *wrapped) {

  return PyObject_CallOneArg(
      (PyObject *) KOS_EVALUATED_PYTYPE(KosProcedureCallableType), wrapped);
}


/* === IterFunction === */


/*
  IterFunction wraps a generator KosFunction bound as __iter__.
  slot_tp_iter requires __iter__ to return an iterator, and calling a
  generator function returns its AsyncGenerator, so a call from Python
  presents that generator as a KosPyIter. Calling a generator function
  evaluates nothing, so neither path involves the demand core: demand
  applies when the KosPyIter is driven. Construction, binding, and
  attribute forwarding are inherited from Demand.
*/


/**
   The Python vectorcall entry: the wrapped function's own vectorcall
   builds the generator, presented as a KosPyIter.
*/
static PyObject *iterfunction_vectorcall(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  PyObject *wrapped = KosDemand_GetWrapped(self);
  PyObject *agen;
  PyObject *iter;

  if (! wrapped) {
    PyErr_SetString(PyExc_TypeError,
                    "IterFunction object is not initialized");
    return NULL;
  }

  agen = PyObject_Vectorcall(wrapped, args, nargsf, kwnames);
  if (! agen || ! KosPyAsyncGen_Check(agen))
    return agen;

  iter = KosIterAdapt_PyIter(agen);
  Py_DECREF(agen);
  return iter;
}


static PyObject *iterfunction_repr(PyObject *self) {

  PyObject *wrapped = KosDemand_GetWrapped(self);

  if (! wrapped)
    return PyUnicode_FromString("<IterFunction ?>");

  PyObject *name = PyObject_GetAttrString(wrapped, "__name__");
  if (! name) {
    PyErr_Clear();
    return PyUnicode_FromString("<IterFunction <lambda>>");
  }

  PyObject *result = PyUnicode_FromFormat("<IterFunction %R>", name);
  Py_DECREF(name);
  return result;
}


/**
   Evaluator install: the wrapped function's own install, which
   completes in hand with the new AsyncGenerator for the Koskari caller
   to iterate natively.
*/
static KosOpResult iterfunction_kos_vinstall(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  PyObject *wrapped = KosDemand_GetWrapped(callable);

  if (! wrapped) {
    PyErr_SetString(PyExc_TypeError,
                    "IterFunction object is not initialized");
    return EVAL_RAISE();
  }

  return KosContext_Call(
      ctx, wrapped, args, nargs, kwnames, tail_call, NULL, NULL);
}


static KosEvaluatedTypeObject KosIterFunctionType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.IterFunction",
    .tp_basicsize = 0,  /* inherited from Demand by PyType_Ready */
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL
        | Py_TPFLAGS_METHOD_DESCRIPTOR,
    .tp_vectorcall_offset = KosDemand_VECTORCALL_OFFSET,
    .tp_doc = "A generator function bound as __iter__: called from "
              "Python, presents its generator as an iterator",
    .tp_repr = iterfunction_repr,
    .tp_call = PyVectorcall_Call,
    .tp_base = KOS_EVALUATED_PYTYPE(KosDemandCallableType),
  },
  .kos_call = {
    .kc_vinstall = iterfunction_kos_vinstall,
    .kc_pycall = NULL,
    .kc_pyvectorcall = iterfunction_vectorcall,
  },
};


/* === Namespace preparation === */


/**
   The class dict for type.__new__: every binding of the body namespace
   except __class__, which the body declares for its methods to close
   over and which stays empty until kos_build_class fills it after the
   metaclass returns. It is skipped by name rather than merged and
   removed because reading the empty slot would raise.
*/
static PyObject *build_default_namespace(PyObject *namespace) {

  PyObject *ns_dict = PyDict_New();
  if (! ns_dict)
    return NULL;

  PyObject *keys = PyMapping_Keys(namespace);
  if (! keys) {
    Py_DECREF(ns_dict);
    return NULL;
  }

  Py_ssize_t n = PyList_GET_SIZE(keys);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *key = PyList_GET_ITEM(keys, i);
    if (PyUnicode_Equal(key, str_class_name))
      continue;

    PyObject *val = PyObject_GetItem(namespace, key);
    if (! val || PyDict_SetItem(ns_dict, key, val) < 0) {
      Py_XDECREF(val);
      Py_DECREF(keys);
      Py_DECREF(ns_dict);
      return NULL;
    }
    Py_DECREF(val);
  }

  Py_DECREF(keys);
  return ns_dict;
}


/**
   Wrap every KosCallable under the listed names in place using wrap
   (KosDemand_New or KosProcedure_New). Both constructors are
   idempotent, so re-running over an already-wrapped namespace is a
   no-op.
*/
static int wrap_name_list(
    PyObject *ns_dict,
    PyObject **names,
    PyObject *(*wrap)(PyObject *)) {

  for (size_t i = 0; names[i]; i++) {
    PyObject *val = PyDict_GetItem(ns_dict, names[i]);
    if (! val)
      continue;

    if (! KosCallable_Check(val))
      continue;

    PyObject *wrapped = wrap(val);
    if (! wrapped)
      return -1;

    if (wrapped != val
        && PyDict_SetItem(ns_dict, names[i], wrapped) < 0) {
      Py_DECREF(wrapped);
      return -1;
    }
    Py_DECREF(wrapped);
  }

  return 0;
}


/**
   Canonicalize KosCallables under the listed names to
   descr_new(Demand(fn)) -- staticmethod(Demand) or classmethod(Demand).
   type_new performs its own staticmethod/classmethod treatment for
   these names, but only for exact plain Python functions
   (PyFunction_Check); a bare KosCallable would otherwise reach the
   synchronous construction machinery unwrapped. An author-written
   staticmethod/classmethod is unwrapped through __func__ and rebuilt
   the same way, so both spellings converge on one canonical form.
   Already-canonical entries and plain Python callables are untouched.
*/
static int canonicalize_name_list(
    PyObject *ns_dict,
    PyObject **names,
    PyTypeObject *descr_type,
    PyObject *(*descr_new)(PyObject *)) {

  for (size_t i = 0; names[i]; i++) {
    PyObject *val = PyDict_GetItem(ns_dict, names[i]);
    if (! val)
      continue;

    PyObject *inner = val;
    PyObject *func_ref = NULL;
    int is_wrapped = (Py_TYPE(val) == descr_type);

    if (is_wrapped) {
      func_ref = PyObject_GetAttr(val, str_func_name);
      if (! func_ref)
        return -1;
      inner = func_ref;
    }

    if (! KosCallable_Check(inner)) {
      Py_XDECREF(func_ref);
      continue;
    }

    PyObject *demanded = KosDemand_New(inner);
    Py_XDECREF(func_ref);
    if (! demanded)
      return -1;

    if (is_wrapped && demanded == inner) {
      /* already descr(Demand(...)): true no-op */
      Py_DECREF(demanded);
      continue;
    }

    PyObject *descr = descr_new(demanded);
    Py_DECREF(demanded);
    if (! descr)
      return -1;

    int r = PyDict_SetItem(ns_dict, names[i], descr);
    Py_DECREF(descr);
    if (r < 0)
      return -1;
  }

  return 0;
}


/**
   Wrap __iter__ in IterFunction when it is bound to a generator
   KosFunction. Anything else under __iter__ is left to the demand pass
   and must return an iterator itself. A re-run finds the IterFunction,
   not a KosFunction, and does nothing.
*/
static int wrap_iter_function(PyObject *ns_dict) {

  PyObject *val = PyDict_GetItem(ns_dict, str_iter_name);
  PyObject *wrapped;
  KosCode *code;
  int r;

  if (! val || ! KosFunction_CheckExact(val))
    return 0;

  code = (KosCode *) KosFunction_CODE(val);
  if (! code || ! KosCode_YIELDS(code))
    return 0;

  wrapped = PyObject_CallOneArg(
      (PyObject *) KOS_EVALUATED_PYTYPE(KosIterFunctionType), val);
  if (! wrapped)
    return -1;

  r = PyDict_SetItem(ns_dict, str_iter_name, wrapped);
  Py_DECREF(wrapped);
  return r;
}


/**
   Apply the wrap passes to the class namespace in place. Only
   KosCallables are touched; plain Python callables are already
   synchronous and pass through untouched. Every pass is idempotent.
   The __iter__ pass runs ahead of the demand pass, which leaves an
   IterFunction as it is (KosDemand_New returns a Demand unchanged).
*/
static int demand_wrap_namespace(PyObject *ns_dict) {

  if (wrap_name_list(ns_dict, procedure_names, KosProcedure_New) < 0)
    return -1;

  if (wrap_iter_function(ns_dict) < 0)
    return -1;

  if (wrap_name_list(ns_dict, demand_names, KosDemand_New) < 0)
    return -1;

  if (canonicalize_name_list(ns_dict, static_names,
                             &PyStaticMethod_Type, PyStaticMethod_New) < 0)
    return -1;

  return canonicalize_name_list(ns_dict, class_names,
                                &PyClassMethod_Type, PyClassMethod_New);
}


/* === MRO entry resolution === */


/**
   The bases the metaclass receives: each base that is not a type but
   defines __mro_entries__ is replaced by the tuple that method returns
   for the bases as written, as CPython's __build_class__ does before
   it calculates the metaclass (Generic[T], NamedTuple, TypedDict).
   Returns a new reference to bases itself when nothing changed, so the
   caller can tell whether to record __orig_bases__.
*/
static PyObject *resolve_mro_entries(PyObject *bases) {

  Py_ssize_t n = PyTuple_GET_SIZE(bases);
  PyObject *out = NULL;   /* built on the first substitution */

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *base = PyTuple_GET_ITEM(bases, i);
    PyObject *meth;
    PyObject *entries;

    if (PyType_Check(base)) {
      if (out && PyList_Append(out, base) < 0)
        goto fail;
      continue;
    }

    meth = PyObject_GetAttr(base, str_mro_entries);
    if (! meth) {
      if (! PyErr_ExceptionMatches(PyExc_AttributeError))
        goto fail;
      PyErr_Clear();
      if (out && PyList_Append(out, base) < 0)
        goto fail;
      continue;
    }

    entries = PyObject_CallOneArg(meth, bases);
    Py_DECREF(meth);
    if (! entries)
      goto fail;
    if (! PyTuple_Check(entries)) {
      Py_DECREF(entries);
      PyErr_SetString(PyExc_TypeError,
                      "__mro_entries__ must return a tuple");
      goto fail;
    }

    if (! out) {
      out = PyList_New(0);
      if (! out) {
        Py_DECREF(entries);
        goto fail;
      }
      for (Py_ssize_t j = 0; j < i; j++) {
        if (PyList_Append(out, PyTuple_GET_ITEM(bases, j)) < 0) {
          Py_DECREF(entries);
          goto fail;
        }
      }
    }
    for (Py_ssize_t j = 0; j < PyTuple_GET_SIZE(entries); j++) {
      if (PyList_Append(out, PyTuple_GET_ITEM(entries, j)) < 0) {
        Py_DECREF(entries);
        goto fail;
      }
    }
    Py_DECREF(entries);
  }

  if (! out) {
    Py_INCREF(bases);
    return bases;
  }
  {
    PyObject *resolved = PyList_AsTuple(out);
    Py_DECREF(out);
    return resolved;
  }

 fail:
  Py_XDECREF(out);
  return NULL;
}


/* === Metaclass resolution === */


/**
   Determine the winning metaclass, CPython __build_class__ style: start
   from the explicit metaclass (or type), then take the most derived of
   it and every base's metaclass. An explicit non-type callable is used
   as given. Returns a new reference, or NULL with PyErr set on a
   metaclass conflict.
*/
static PyObject *resolve_metaclass(
    PyObject *bases,
    PyObject *metaclass_or_null) {

  PyObject *winner = metaclass_or_null
      ? metaclass_or_null
      : (PyObject *) &PyType_Type;

  if (! PyType_Check(winner)) {
    Py_INCREF(winner);
    return winner;
  }

  Py_ssize_t n = PyTuple_GET_SIZE(bases);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *base = PyTuple_GET_ITEM(bases, i);
    if (! PyType_Check(base))
      continue;

    PyTypeObject *base_meta = Py_TYPE(base);
    if (PyType_IsSubtype((PyTypeObject *) winner, base_meta))
      continue;
    if (PyType_IsSubtype(base_meta, (PyTypeObject *) winner)) {
      winner = (PyObject *) base_meta;
      continue;
    }

    PyErr_SetString(PyExc_TypeError,
        "metaclass conflict: the metaclass of a derived class must be a "
        "(non-strict) subclass of the metaclasses of all its bases");
    return NULL;
  }

  Py_INCREF(winner);
  return winner;
}


static PyObject *resolve_prepare_namespace(
    PyObject *metaclass,
    PyObject *name,
    PyObject *bases) {

  PyObject *prepare = PyObject_GetAttrString(metaclass, "__prepare__");
  if (! prepare) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return PyDict_New();
    }
    return NULL;
  }

  PyObject *prepared = PyObject_CallFunctionObjArgs(prepare, name, bases, NULL);
  Py_DECREF(prepare);
  return prepared;
}


/* === instance __call__ inlining === */


/**
   tp_call shim installed on classes whose __call__ resolves to a
   KosCallable. It doubles as a sentinel: the evaluator recognizes it via
   KosInstanceCall_Check and inlines the call through the target's install
   slot instead of starting a fresh eval loop (see kos_call_dispatch), with
   self injected when the target binds its receiver and without it for a
   non-descriptor such as a partial. This shim follows the same rule for a
   call from Python.

   Unlike the demand-wrapped dunders this preserves async semantics -- a
   bound async __call__ returns a coroutine here, exactly as CPython's
   slot_tp_call would. Self injection follows ordinary descriptor binding:
   getattr yields a bound method for a KosFunction/Demand accessor.
*/
PyObject *kos_instance_call(PyObject *self, PyObject *args, PyObject *kwds) {

  PyTypeObject *tp = Py_TYPE(self);
  PyObject *descr = PyType_Lookup(tp, str_call_name);
  PyObject *call;
  PyObject *result;

  if (! descr) {
    PyErr_Format(PyExc_TypeError, "'%.200s' object is not callable",
                 tp->tp_name);
    return NULL;
  }

  /* CPython's slot_tp_call rule: resolved on the type, bound through
     its descriptor when it has one, called as is otherwise. */
  if (Py_TYPE(descr)->tp_descr_get) {
    call = Py_TYPE(descr)->tp_descr_get(descr, self, (PyObject *) tp);
    if (! call)
      return NULL;
  } else {
    Py_INCREF(descr);
    call = descr;
  }

  result = PyObject_Call(call, args, kwds);
  Py_DECREF(call);
  return result;
}


/**
   Resolve the __call__ of self's type for inline dispatch: a BORROWED
   reference to the KosCallable found on the type, or NULL when __call__ is
   absent or not a KosCallable. *binds says whether that callable binds
   its receiver (its type has tp_descr_get: a function, the Demand family),
   in which case the evaluator injects self as the first positional itself
   and no bound method is built; a non-descriptor such as a partial is
   called without self, as CPython's slot_tp_call and kos_instance_call
   call it. Uses the type method cache (PyType_Lookup) -- no allocation,
   no descriptor binding, no transient string.
*/
PyObject *kos_instance_call_target(PyObject *self, int *binds) {

  PyObject *fn = PyType_Lookup(Py_TYPE(self), str_call_name);

  *binds = 0;
  if (! fn || ! KosCallable_Check(fn))
    return NULL;
  *binds = Py_TYPE(fn)->tp_descr_get != NULL;
  return fn;
}


/**
   Install the kos_instance_call sentinel on a freshly built class whose
   __call__ resolves to a KosCallable. A plain Python __call__ keeps
   CPython's slot_tp_call.
*/
static void install_call_sentinel(PyTypeObject *type) {

  PyObject *fn = PyType_Lookup(type, str_call_name);
  if (fn && KosCallable_Check(fn)) {
    type->tp_call = kos_instance_call;
    PyType_Modified(type);
  }
}


/* === kos_build_class === */


PyObject *kos_build_class(
    PyObject *name,
    PyObject *bases,
    PyObject *namespace,
    PyObject *metaclass_or_null) {

  if (! name || ! PyUnicode_Check(name)) {
    PyErr_SetString(PyExc_TypeError, "name must be a str");
    return NULL;
  }

  PyObject *bases_tuple = bases;
  if (! PyTuple_CheckExact(bases_tuple)) {
    bases_tuple = PySequence_Tuple(bases);
    if (! bases_tuple)
      return NULL;
  } else {
    Py_INCREF(bases_tuple);
  }

  PyObject *ns_dict = build_default_namespace(namespace);
  if (! ns_dict) {
    Py_DECREF(bases_tuple);
    return NULL;
  }

  if (demand_wrap_namespace(ns_dict) < 0) {
    Py_DECREF(ns_dict);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  {
    PyObject *resolved = resolve_mro_entries(bases_tuple);
    if (! resolved) {
      Py_DECREF(ns_dict);
      Py_DECREF(bases_tuple);
      return NULL;
    }
    if (resolved != bases_tuple
        && PyDict_SetItem(ns_dict, str_orig_bases, bases_tuple) < 0) {
      Py_DECREF(resolved);
      Py_DECREF(ns_dict);
      Py_DECREF(bases_tuple);
      return NULL;
    }
    Py_DECREF(bases_tuple);
    bases_tuple = resolved;
  }

  PyObject *metaclass = resolve_metaclass(bases_tuple, metaclass_or_null);
  if (! metaclass) {
    Py_DECREF(ns_dict);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  PyObject *prepared_ns = resolve_prepare_namespace(metaclass, name, bases_tuple);
  if (! prepared_ns) {
    Py_DECREF(metaclass);
    Py_DECREF(ns_dict);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  Py_ssize_t pos = 0;
  PyObject *key = NULL;
  PyObject *value = NULL;
  while (PyDict_Next(ns_dict, &pos, &key, &value)) {
    if (PyObject_SetItem(prepared_ns, key, value) < 0) {
      Py_DECREF(ns_dict);
      Py_DECREF(prepared_ns);
      Py_DECREF(metaclass);
      Py_DECREF(bases_tuple);
      return NULL;
    }
  }
  Py_DECREF(ns_dict);

  PyObject *cls = PyObject_CallFunctionObjArgs(
      metaclass,
      name,
      bases_tuple,
      prepared_ns,
      NULL);

  Py_DECREF(prepared_ns);
  Py_DECREF(metaclass);
  Py_DECREF(bases_tuple);

  if (! cls)
    return NULL;

  if (PyType_Check(cls))
    install_call_sentinel((PyTypeObject *) cls);

  /* Fill __class__ on the body namespace, the way type.__new__ fills
     the __classcell__ a class body created. Methods that mention
     __class__, the (super) expansion included, closed over this binding
     when they were defined; from here on they resolve it to their
     defining class. build_default_namespace keeps the name out of the
     class dict. */
  {
    int rc = KosNamespace_Check(namespace)
        ? KosNamespace_Store(namespace, str_class_name, cls)
        : PyObject_SetItem(namespace, str_class_name, cls);
    if (rc < 0) {
      Py_DECREF(cls);
      return NULL;
    }
  }

  return cls;
}


/* === Unit lifecycle === */


static int intern_name_list(const char **specs, PyObject **out) {

  size_t i;
  for (i = 0; specs[i]; i++) {
    out[i] = PyUnicode_InternFromString(specs[i]);
    if (! out[i])
      return -1;
  }
  out[i] = NULL;
  return 0;
}


static void clear_name_list(PyObject **names) {

  for (size_t i = 0; names[i]; i++)
    Py_CLEAR(names[i]);
}


int koskari_object_init(PyObject *mod) {

  (void) mod;

  STR_CONST(str_call_name, "__call__");
  STR_CONST(str_func_name, "__func__");
  STR_CONST(str_class_name, "__class__");
  STR_CONST(str_iter_name, "__iter__");
  STR_CONST(str_mro_entries, "__mro_entries__");
  STR_CONST(str_orig_bases, "__orig_bases__");
  if (! str_call_name || ! str_func_name || ! str_class_name
      || ! str_iter_name || ! str_mro_entries || ! str_orig_bases)
    return -1;

  if (intern_name_list(procedure_dunders, procedure_names) < 0
      || intern_name_list(static_dunders, static_names) < 0
      || intern_name_list(class_dunders, class_names) < 0
      || intern_name_list(demand_dunders, demand_names) < 0)
    return -1;

  /* Demand must be ready first: koskari_demand_init runs earlier in the
     unit order (see lib.c). */
  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosProcedureCallableType)) < 0
      || PyType_Ready(KOS_EVALUATED_PYTYPE(KosIterFunctionType)) < 0)
    return -1;

  return 0;
}


int koskari_object_clear(PyObject *mod) {

  Py_CLEAR(str_call_name);
  Py_CLEAR(str_mro_entries);
  Py_CLEAR(str_orig_bases);
  Py_CLEAR(str_func_name);
  Py_CLEAR(str_class_name);
  Py_CLEAR(str_iter_name);
  clear_name_list(procedure_names);
  clear_name_list(static_names);
  clear_name_list(class_names);
  clear_name_list(demand_names);
  (void) mod;
  return 0;
}


// The end.
