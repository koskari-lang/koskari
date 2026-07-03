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
   koskari/lib/partial.c

   KosPartial: a standalone native KosCallable with the behavior of
   functools.partial.

   It binds a callable together with leading positional args and keyword args;
   calling it merges those with the call-site args and invokes the inner
   callable. Construction flattens nested KosPartials, the same way
   functools.partial collapses partial(partial(f, a), b).

   Unlike functools.partial it is not in that type's inheritance tree -- the
   match is by duck typing, not isinstance. The payoff is that it is a static
   KosCallableTypeObject: when koskari bytecode calls it and the inner callable
   is a flattenable KosCallable, the install slot merges args and tail-calls
   straight through, reusing the single frame do_frame_prep prepared, with no
   intermediate KosContext.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GPL v.3
*/


#include "lib.h"


/* === instance === */


typedef struct {
  PyObject_HEAD
  PyObject *func;        /* callable; non-NULL once constructed */
  PyObject *args;        /* tuple of bound positional args; non-NULL */
  PyObject *keywords;    /* dict of bound keyword args; non-NULL */
  PyObject *dict;        /* instance __dict__; lazily created, may be NULL */
  PyObject *weakreflist; /* weak references; may be NULL */
} KosPartialObject;


/* === argument merging === */


/**
   Merge bound positional args with call-site positional args.

   Returns a new reference to the combined tuple, borrowing an operand without
   allocation when either side is empty. call_args may be NULL (treated as
   empty). Returns NULL with PyErr set on failure.
*/
static PyObject *kospartial_merge_args(
    PyObject *bound_args,
    PyObject *call_args) {

  if (! call_args || PyTuple_GET_SIZE(call_args) == 0) {
    Py_INCREF(bound_args);
    return bound_args;
  }

  if (PyTuple_GET_SIZE(bound_args) == 0) {
    Py_INCREF(call_args);
    return call_args;
  }

  return PySequence_Concat(bound_args, call_args);
}


/**
   Merge bound keyword args with call-site keyword args into *out.

   *out is set to a new reference, to a borrowed operand (incref'd) when the
   other side is empty, or to NULL when both are empty. call_kw may be NULL.
   Returns 0 on success, -1 with PyErr set on failure.
*/
static int kospartial_merge_kwds(
    PyObject *bound_kw,
    PyObject *call_kw,
    PyObject **out) {

  int call_empty = (! call_kw) || (PyDict_Size(call_kw) == 0);

  if (PyDict_Size(bound_kw) == 0) {
    if (call_empty) {
      *out = NULL;
      return 0;
    }
    Py_INCREF(call_kw);
    *out = call_kw;
    return 0;
  }

  if (call_empty) {
    Py_INCREF(bound_kw);
    *out = bound_kw;
    return 0;
  }

  PyObject *merged = PyDict_Copy(bound_kw);
  if (! merged)
    return -1;

  if (PyDict_Update(merged, call_kw) < 0) {
    Py_DECREF(merged);
    return -1;
  }

  *out = merged;
  return 0;
}


/* === Python-side call === */


/**
   tp_call: merge bound args with call args and invoke func. Returns whatever
   func returns (a coroutine for an async Koskari callable; a value otherwise).
*/
static PyObject *kospartial_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosPartialObject *p = (KosPartialObject *) self;

  PyObject *merged_args = kospartial_merge_args(p->args, args);
  if (! merged_args)
    return NULL;

  PyObject *merged_kw = NULL;
  if (kospartial_merge_kwds(p->keywords, kwds, &merged_kw) < 0) {
    Py_DECREF(merged_args);
    return NULL;
  }

  PyObject *result = PyObject_Call(p->func, merged_args, merged_kw);
  Py_DECREF(merged_args);
  Py_XDECREF(merged_kw);
  return result;
}


/* === KosCallable install === */


/**
   Return 1 if inner is a KosCallable that wants a frame (so it can be installed
   directly into the frame prepared for this partial), 0 otherwise. Delegates to
   the inner callable's per-instance wants_frame hook when present.
*/
static int kospartial_inner_is_flattenable(PyObject *inner) {

  if (! inner)
    return 0;

  if (! KosCallable_Check(inner))
    return 0;

  KosCallableTypeObject *ctyp = (KosCallableTypeObject *) Py_TYPE(inner);

  if (ctyp->tp_kos_wants_frame_fn)
    return ctyp->tp_kos_wants_frame_fn(inner);

  return ctyp->tp_kos_wants_frame;
}


static int kospartial_wants_frame(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;
  return kospartial_inner_is_flattenable(p->func);
}


/**
   tp_kos_install.

   Flattenable inner: merge bound + call args and tail-call through to func,
   reusing the frame do_frame_prep prepared for this partial. tail_call is
   forwarded unchanged -- the frame was prepared with this partial's
   wants_frame (which delegates to func), so func's install slot fills exactly
   that frame.

   Otherwise: invoke via the Python call path (kospartial_call merges) and push
   the result, mirroring the plain-Python call fallback.
*/
static KosOpResult kospartial_kos_install(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosPartialObject *p = (KosPartialObject *) callable;
  PyObject *inner = p->func;

  if (! kospartial_inner_is_flattenable(inner)) {
    PyObject *empty = NULL;
    PyObject *call_args = args_tuple;
    if (! call_args) {
      empty = PyTuple_New(0);
      if (! empty)
        return EVAL_RAISE();
      call_args = empty;
    }

    PyObject *result = kospartial_call(callable, call_args, kwds_dict);
    Py_XDECREF(empty);
    if (! result)
      return EVAL_RAISE();

    if (KosEvalState_StackPUSH(es, result) < 0)
      return EVAL_RAISE();  /* PUSH steals ref; already decremented on failure */

    return EVAL_NORMAL();
  }

  PyObject *merged_args = kospartial_merge_args(p->args, args_tuple);
  if (! merged_args)
    return EVAL_RAISE();

  PyObject *merged_kw = NULL;
  if (kospartial_merge_kwds(p->keywords, kwds_dict, &merged_kw) < 0) {
    Py_DECREF(merged_args);
    return EVAL_RAISE();
  }

  KosOpResult r = KosCallable_CallArgs(es, inner, merged_args, merged_kw,
                                       tail_call);
  Py_DECREF(merged_args);
  Py_XDECREF(merged_kw);
  return r;
}


/* === construction === */


/**
   KosPartial(func, *args, **keywords). func must be callable. A KosPartial
   func is flattened: its bound args/keywords are folded in and its own func is
   used, so the result never nests.
*/
static PyObject *kospartial_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  if (PyTuple_GET_SIZE(args) < 1) {
    PyErr_SetString(PyExc_TypeError,
                    "KosPartial takes at least one argument");
    return NULL;
  }

  PyObject *func = PyTuple_GET_ITEM(args, 0);
  if (! PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "the first argument must be callable");
    return NULL;
  }

  PyObject *rest = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
  if (! rest)
    return NULL;

  PyObject *new_args = NULL;
  PyObject *new_kw = NULL;

  if (KosPartial_CheckExact(func)) {
    KosPartialObject *inner = (KosPartialObject *) func;

    new_args = PySequence_Concat(inner->args, rest);
    Py_DECREF(rest);
    if (! new_args)
      return NULL;

    new_kw = PyDict_Copy(inner->keywords);
    if (! new_kw) {
      Py_DECREF(new_args);
      return NULL;
    }
    if (kwds && PyDict_Update(new_kw, kwds) < 0) {
      Py_DECREF(new_args);
      Py_DECREF(new_kw);
      return NULL;
    }

    func = inner->func;  /* borrowed; incref below */

  } else {
    new_args = rest;  /* steal the slice reference */
    new_kw = kwds ? PyDict_Copy(kwds) : PyDict_New();
    if (! new_kw) {
      Py_DECREF(new_args);
      return NULL;
    }
  }

  KosPartialObject *self = (KosPartialObject *) type->tp_alloc(type, 0);
  if (! self) {
    Py_DECREF(new_args);
    Py_DECREF(new_kw);
    return NULL;
  }

  Py_INCREF(func);
  self->func = func;
  self->args = new_args;
  self->keywords = new_kw;
  self->dict = NULL;
  self->weakreflist = NULL;
  return (PyObject *) self;
}


/* === repr === */


static PyObject *kospartial_repr(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;

  int status = Py_ReprEnter(self);
  if (status != 0)
    return (status < 0) ? NULL : PyUnicode_FromString("...");

  PyObject *result = NULL;
  PyObject *parts = PyList_New(0);
  if (! parts)
    goto done;

  {
    PyObject *fr = PyObject_Repr(p->func);
    if (! fr || PyList_Append(parts, fr) < 0) {
      Py_XDECREF(fr);
      goto done;
    }
    Py_DECREF(fr);
  }

  Py_ssize_t na = PyTuple_GET_SIZE(p->args);
  for (Py_ssize_t i = 0; i < na; i++) {
    PyObject *ar = PyObject_Repr(PyTuple_GET_ITEM(p->args, i));
    if (! ar || PyList_Append(parts, ar) < 0) {
      Py_XDECREF(ar);
      goto done;
    }
    Py_DECREF(ar);
  }

  {
    PyObject *k, *v;
    Py_ssize_t pos = 0;
    while (PyDict_Next(p->keywords, &pos, &k, &v)) {
      PyObject *vr = PyObject_Repr(v);
      if (! vr)
        goto done;
      PyObject *kv = PyUnicode_FromFormat("%S=%U", k, vr);
      Py_DECREF(vr);
      if (! kv || PyList_Append(parts, kv) < 0) {
        Py_XDECREF(kv);
        goto done;
      }
      Py_DECREF(kv);
    }
  }

  {
    PyObject *sep = PyUnicode_FromString(", ");
    if (! sep)
      goto done;
    PyObject *joined = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    if (! joined)
      goto done;
    result = PyUnicode_FromFormat("koskari.lib.KosPartial(%U)", joined);
    Py_DECREF(joined);
  }

done:
  Py_XDECREF(parts);
  Py_ReprLeave(self);
  return result;
}


/**
   __kos_repr__: the Koskari s-expression form,

     (partial <func> <arg>... <kwd>: <val> ...)

   func and each positional render via KosUtils_Repr (their own kos form);
   keywords render as a name-plus-colon token followed by the value's kos
   form. Registered into the kos_repr registry by koskari_partial_init.
*/
static PyObject *kospartial_kos_repr(
    PyObject *self,
    PyObject *Py_UNUSED(ignored)) {

  KosPartialObject *p = (KosPartialObject *) self;

  int status = Py_ReprEnter(self);
  if (status != 0)
    return (status < 0) ? NULL : PyUnicode_FromString("...");

  PyObject *result = NULL;
  PyObject *parts = PyList_New(0);
  if (! parts)
    goto done;

  {
    PyObject *head = PyUnicode_FromString("partial");
    if (! head || PyList_Append(parts, head) < 0) {
      Py_XDECREF(head);
      goto done;
    }
    Py_DECREF(head);
  }

  {
    PyObject *fr = KosUtils_Repr(p->func);
    if (! fr || PyList_Append(parts, fr) < 0) {
      Py_XDECREF(fr);
      goto done;
    }
    Py_DECREF(fr);
  }

  Py_ssize_t na = PyTuple_GET_SIZE(p->args);
  for (Py_ssize_t i = 0; i < na; i++) {
    PyObject *ar = KosUtils_Repr(PyTuple_GET_ITEM(p->args, i));
    if (! ar || PyList_Append(parts, ar) < 0) {
      Py_XDECREF(ar);
      goto done;
    }
    Py_DECREF(ar);
  }

  {
    PyObject *k, *v;
    Py_ssize_t pos = 0;
    while (PyDict_Next(p->keywords, &pos, &k, &v)) {
      PyObject *kwd = PyUnicode_FromFormat("%U:", k);
      if (! kwd || PyList_Append(parts, kwd) < 0) {
        Py_XDECREF(kwd);
        goto done;
      }
      Py_DECREF(kwd);

      PyObject *vr = KosUtils_Repr(v);
      if (! vr || PyList_Append(parts, vr) < 0) {
        Py_XDECREF(vr);
        goto done;
      }
      Py_DECREF(vr);
    }
  }

  {
    PyObject *sep = PyUnicode_FromString(" ");
    if (! sep)
      goto done;
    PyObject *joined = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    if (! joined)
      goto done;
    result = PyUnicode_FromFormat("(%U)", joined);
    Py_DECREF(joined);
  }

done:
  Py_XDECREF(parts);
  Py_ReprLeave(self);
  return result;
}


/* === pickle === */


static PyObject *kospartial_reduce(PyObject *self, PyObject *unused) {

  KosPartialObject *p = (KosPartialObject *) self;
  PyObject *d = p->dict ? p->dict : Py_None;

  (void) unused;
  return Py_BuildValue("O(O)(OOOO)",
      (PyObject *) Py_TYPE(self), p->func,
      p->func, p->args, p->keywords, d);
}


static PyObject *kospartial_setstate(PyObject *self, PyObject *state) {

  KosPartialObject *p = (KosPartialObject *) self;

  if (! PyTuple_Check(state) || PyTuple_GET_SIZE(state) != 4) {
    PyErr_SetString(PyExc_TypeError, "invalid KosPartial state");
    return NULL;
  }

  PyObject *func = PyTuple_GET_ITEM(state, 0);
  PyObject *fnargs = PyTuple_GET_ITEM(state, 1);
  PyObject *kw = PyTuple_GET_ITEM(state, 2);
  PyObject *d = PyTuple_GET_ITEM(state, 3);

  if (! PyCallable_Check(func) || ! PyTuple_Check(fnargs)
      || (kw != Py_None && ! PyDict_Check(kw))) {
    PyErr_SetString(PyExc_TypeError, "invalid KosPartial state");
    return NULL;
  }

  PyObject *new_args = PySequence_Tuple(fnargs);
  if (! new_args)
    return NULL;

  PyObject *new_kw = (kw == Py_None) ? PyDict_New() : PyDict_Copy(kw);
  if (! new_kw) {
    Py_DECREF(new_args);
    return NULL;
  }

  Py_INCREF(func);
  Py_SETREF(p->func, func);
  Py_SETREF(p->args, new_args);
  Py_SETREF(p->keywords, new_kw);

  if (d == Py_None) {
    Py_CLEAR(p->dict);
  } else {
    Py_INCREF(d);
    Py_XSETREF(p->dict, d);
  }

  Py_RETURN_NONE;
}


/* === GC + dealloc === */


static int kospartial_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPartialObject *p = (KosPartialObject *) self;
  Py_VISIT(p->func);
  Py_VISIT(p->args);
  Py_VISIT(p->keywords);
  Py_VISIT(p->dict);
  return 0;
}


static int kospartial_clear(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;
  Py_CLEAR(p->func);
  Py_CLEAR(p->args);
  Py_CLEAR(p->keywords);
  Py_CLEAR(p->dict);
  return 0;
}


static void kospartial_dealloc(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;

  PyObject_GC_UnTrack(self);
  if (p->weakreflist)
    PyObject_ClearWeakRefs(self);

  Py_CLEAR(p->func);
  Py_CLEAR(p->args);
  Py_CLEAR(p->keywords);
  Py_CLEAR(p->dict);
  Py_TYPE(self)->tp_free(self);
}


/* === KosPartial type === */


static PyMemberDef kospartial_members[] = {
  {"func", T_OBJECT, offsetof(KosPartialObject, func), READONLY,
   "function object to use in future partial calls"},
  {"args", T_OBJECT, offsetof(KosPartialObject, args), READONLY,
   "tuple of arguments to future partial calls"},
  {"keywords", T_OBJECT, offsetof(KosPartialObject, keywords), READONLY,
   "dictionary of keyword arguments to future partial calls"},
  {NULL},
};


static PyGetSetDef kospartial_getset[] = {
  {"__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL},
  {NULL},
};


static PyMethodDef kospartial_methods[] = {
  {"__reduce__", kospartial_reduce, METH_NOARGS, NULL},
  {"__setstate__", kospartial_setstate, METH_O, NULL},
  {"__kos_repr__", kospartial_kos_repr, METH_NOARGS,
   "P.__kos_repr__() -> str\n"
   "Koskari-style (partial func args... kwd: val ...) form."},
  {NULL},
};


KosCallableTypeObject KosPartialCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.KosPartial",
    .tp_basicsize = sizeof(KosPartialObject),
    .tp_itemsize = 0,
    .tp_dealloc = kospartial_dealloc,
    .tp_repr = kospartial_repr,
    .tp_call = kospartial_call,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_setattro = PyObject_GenericSetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_doc = "KosPartial(func, *args, **keywords) - native KosCallable with "
              "the behavior of functools.partial",
    .tp_traverse = kospartial_traverse,
    .tp_clear = kospartial_clear,
    .tp_weaklistoffset = offsetof(KosPartialObject, weakreflist),
    .tp_methods = kospartial_methods,
    .tp_members = kospartial_members,
    .tp_getset = kospartial_getset,
    .tp_dictoffset = offsetof(KosPartialObject, dict),
    .tp_new = kospartial_new,
    .tp_base = KOS_CALLABLE_PYTYPE(KosCallableType),
  },
  .tp_kos_install = kospartial_kos_install,
  .tp_kos_vinstall = NULL,
  .tp_kos_wants_frame = 0,
  .tp_kos_wants_frame_fn = kospartial_wants_frame,
};


/* === module lifecycle === */


int koskari_partial_init(PyObject *mod) {

  if (PyType_Ready(KOS_CALLABLE_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  if (KosUtils_ReprRegisterBuiltin(
          (PyObject *) KOS_CALLABLE_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyDict_SetItemString(dict, "KosPartial",
                           (PyObject *) KOS_CALLABLE_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  return 0;
}


int koskari_partial_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
