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
   koskari.lib values

   Native values objects for multiple return values

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"


/* === string constants === */


static PyObject *_str_values_paren = NULL;
static PyObject *_str_comma_space = NULL;
static PyObject *_str_close_paren = NULL;
static PyObject *_str_equals = NULL;
static PyObject *_str_empty = NULL;


/* === helper function === */


static PyObject *kos_quoted(PyObject *u) {
    /*
      Return a quoted string representation for error messages.
      Returns new reference.
    */
    PyObject *repr = PyObject_Repr(u);
    return repr;
}


/* === values type methods === */


static PyObject *values_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

    (void)type;  // to let the compiler know we're not using it
    return KosValues_New(args, kwds);
}


static void values_dealloc(PyObject *self) {
    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_BEGIN(self, values_dealloc);

    Py_CLEAR(KosValues_ARGS(self));
    Py_CLEAR(KosValues_KWDS(self));

    PyObject_GC_Del(self);
    Py_TRASHCAN_END;
}


static int values_traverse(PyObject *self, visitproc visit, void *arg) {
    Py_VISIT(KosValues_ARGS(self));
    Py_VISIT(KosValues_KWDS(self));
    return 0;
}


static int values_clear(PyObject *self) {
    Py_CLEAR(KosValues_ARGS(self));
    Py_CLEAR(KosValues_KWDS(self));
    return 0;
}


static PyObject *values_iter(PyObject *self) {
    return PyObject_GetIter(KosValues_ARGS(self));
}


static Py_ssize_t values_args_length(PyObject *self) {
    return PyTuple_GET_SIZE(KosValues_ARGS(self));
}


static PyObject *values_args_getitem(PyObject *self, Py_ssize_t index) {
    return PySequence_GetItem(KosValues_ARGS(self), index);
}


static Py_ssize_t values_kwds_length(PyObject *self) {
    PyObject *kwds = KosValues_KWDS(self);
    return kwds ? PyDict_Size(kwds) : 0;
}


static PyObject *values_kwds_getitem(
    PyObject *self,
    PyObject *key) {

    if (PyLong_CheckExact(key)) {
        return PySequence_GetItem(KosValues_ARGS(self),
                                  PyLong_AsSsize_t(key));

    } else {
        PyObject *kwds = KosValues_KWDS(self);
        PyObject *result = kwds ? PyDict_GetItem(kwds, key) : NULL;

        if (result) {
            Py_INCREF(result);
            return result;

        } else {
            /* we do our own error handling because result could be NULL
               either because there was a NULL keyword dict, or because of
               an actual NULL result from GetItem. in either of those cases,
               we want to emit the same KeyError */
            PyObject *quoted = kos_quoted(key);
            PyErr_SetObject(PyExc_KeyError, quoted);
            Py_XDECREF(quoted);
            return NULL;
        }
    }
}


/**
   Merge stored args/kwds with an invocation.

   inv_args is the full call-site positional tuple whose first element is the
   target callable. On success *out_target is borrowed, *out_args is a new
   reference, and *out_kw is a new reference or NULL. Returns 0 on success, -1
   with PyErr set on failure.
*/
static int values_merge(
    KosValues *s,
    PyObject *inv_args,
    PyObject *inv_kwds,
    PyObject **out_target,
    PyObject **out_args,
    PyObject **out_kw) {

  PyObject *call_args = NULL;
  PyObject *call_kwds = NULL;
  PyObject *tmp = NULL;
  Py_ssize_t inv_n;

  if (! inv_args || ! PyTuple_GET_SIZE(inv_args)) {
    PyErr_SetString(PyExc_TypeError, "values objects must be called with"
                    " at least one argument, the function to apply");
    return -1;
  }

  *out_target = PyTuple_GET_ITEM(inv_args, 0);
  inv_n = PyTuple_GET_SIZE(inv_args);

  if (inv_n > 1) {
    tmp = PySequence_GetSlice(inv_args, 1, inv_n);
    if (! tmp)
      return -1;

    if (PyTuple_GET_SIZE(KosValues_ARGS(s))) {
      call_args = PySequence_Concat(KosValues_ARGS(s), tmp);
      Py_DECREF(tmp);
      if (! call_args)
        return -1;
    } else {
      call_args = tmp;
    }
  } else {
    call_args = KosValues_ARGS(s);
    Py_INCREF(call_args);
  }

  if (inv_kwds && PyDict_Size(inv_kwds)) {
    if (KosValues_KWDS(s) && PyDict_Size(KosValues_KWDS(s))) {
      call_kwds = PyDict_Copy(KosValues_KWDS(s));
      if (! call_kwds) {
        Py_DECREF(call_args);
        return -1;
      }
      if (PyDict_Update(call_kwds, inv_kwds) < 0) {
        Py_DECREF(call_args);
        Py_DECREF(call_kwds);
        return -1;
      }
    } else {
      call_kwds = inv_kwds;
      Py_INCREF(call_kwds);
    }
  } else {
    call_kwds = KosValues_KWDS(s);
    Py_XINCREF(call_kwds);
  }

  *out_args = call_args;
  *out_kw = call_kwds;
  return 0;
}


static PyObject *values_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosValues *s = (KosValues *) self;
  PyObject *target = NULL;
  PyObject *call_args = NULL;
  PyObject *call_kwds = NULL;
  PyObject *result = NULL;

  if (values_merge(s, args, kwds, &target, &call_args, &call_kwds) < 0)
    return NULL;

  result = PyObject_Call(target, call_args, call_kwds);
  Py_DECREF(call_args);
  Py_XDECREF(call_kwds);
  return result;
}


/* === KosCallable install === */


/**
   Return 1 if target is a KosCallable that wants a frame (flattenable into the
   frame do_frame_prep prepared for values), 0 otherwise.
*/
static int values_target_is_flattenable(PyObject *target) {

  if (! target)
    return 0;

  if (! KosCallable_Check(target))
    return 0;

  return KosCallable_WantsFrame(target);
}


/**
   After merge: Case A flattens through bare KosCallable_CallArgs; Case B
   reconciles the W=1 surplus frame then uses full KosEvalState_CallArgs.
   merged_args / merged_kw are borrowed for the duration of the call.
*/
static KosOpResult values_dispatch(
    KosEvalState *es,
    PyObject *target,
    PyObject *merged_args,
    PyObject *merged_kw,
    int tail_call) {

  if (values_target_is_flattenable(target))
    return KosCallable_CallArgs(es, target, merged_args, merged_kw,
                                tail_call);

  if (! tail_call) {
    if (kos_dyn_pop_frame(es) < 0)
      return EVAL_RAISE();
    return KosEvalState_CallArgs(es, target, merged_args, merged_kw, 0);
  }

  /* Case B, tail. do_frame_prep already reset the values frame. When that
     frame has a parent, KosEvalState_CallArgs(tail=1) pops it via
     tail_prep_py_call. When it is the root, the reset cleared bytecode that
     CONTROL_RETURNED would still walk -- deliver the result with RETURN. */
  {
    KosDynFrame *frame = KosEvalState_FRAME(es);
    KosDynNode *parent = frame
        ? KosDynNode_PARENT((KosDynNode *) frame) : NULL;

    if (! parent) {
      if (KosCallable_Check(target)) {
        KosOpResult r = KosEvalState_CallArgs(
            es, target, merged_args, merged_kw, 0);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        {
          PyObject *rv = KosEvalState_StackPopRef(es);
          if (! rv)
            return EVAL_RAISE();
          return EVAL_RETURN(rv);
        }
      }

      {
        PyObject *result = NULL;
        if (merged_args)
          result = PyObject_Call(target, merged_args, merged_kw);
        else if (merged_kw) {
          PyObject *empty = PyTuple_New(0);
          if (! empty)
            return EVAL_RAISE();
          result = PyObject_Call(target, empty, merged_kw);
          Py_DECREF(empty);
        } else {
          result = PyObject_CallNoArgs(target);
        }
        if (! result)
          return EVAL_RAISE();
        return EVAL_RETURN(result);
      }
    }
  }

  return KosEvalState_CallArgs(es, target, merged_args, merged_kw, 1);
}


static KosOpResult values_kos_install(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosValues *s = (KosValues *) callable;
  PyObject *target = NULL;
  PyObject *merged_args = NULL;
  PyObject *merged_kw = NULL;
  KosOpResult r;

  if (values_merge(s, args_tuple, kwds_dict, &target, &merged_args,
                   &merged_kw) < 0)
    return EVAL_RAISE();

  r = values_dispatch(es, target, merged_args, merged_kw, tail_call);
  Py_DECREF(merged_args);
  Py_XDECREF(merged_kw);
  return r;
}


static KosOpResult values_kos_vinstall(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosValues *s = (KosValues *) callable;
  PyObject *inv_args = NULL;
  PyObject *inv_kwds = NULL;
  PyObject *target = NULL;
  PyObject *merged_args = NULL;
  PyObject *merged_kw = NULL;
  KosOpResult r;
  Py_ssize_t i;
  Py_ssize_t nkw;

  if (nargs < 1) {
    PyErr_SetString(PyExc_TypeError, "values objects must be called with"
                    " at least one argument, the function to apply");
    return EVAL_RAISE();
  }

  inv_args = PyTuple_New(nargs);
  if (! inv_args)
    return EVAL_RAISE();

  for (i = 0; i < nargs; i++) {
    Py_INCREF(args[i]);
    PyTuple_SET_ITEM(inv_args, i, args[i]);
  }

  if (kwnames) {
    nkw = PyTuple_GET_SIZE(kwnames);
    inv_kwds = PyDict_New();
    if (! inv_kwds) {
      Py_DECREF(inv_args);
      return EVAL_RAISE();
    }
    for (i = 0; i < nkw; i++) {
      if (PyDict_SetItem(inv_kwds, PyTuple_GET_ITEM(kwnames, i),
                         args[nargs + i]) < 0) {
        Py_DECREF(inv_args);
        Py_DECREF(inv_kwds);
        return EVAL_RAISE();
      }
    }
  }

  if (values_merge(s, inv_args, inv_kwds, &target, &merged_args,
                   &merged_kw) < 0) {
    Py_DECREF(inv_args);
    Py_XDECREF(inv_kwds);
    return EVAL_RAISE();
  }

  Py_DECREF(inv_args);
  Py_XDECREF(inv_kwds);

  r = values_dispatch(es, target, merged_args, merged_kw, tail_call);
  Py_DECREF(merged_args);
  Py_XDECREF(merged_kw);
  return r;
}


static PyObject *values_repr(PyObject *self) {
    KosValues *s = (KosValues *) self;
    PyObject *tmp = NULL;
    Py_ssize_t count = 0, limit = 0;
    PyObject *key = NULL, *value = NULL;

    /* "values()"
       "values(1, 2, 3)"
       "values(foo=4, bar=5)"
       "values(1, 2, 3, foo=4, bar=5)" */

    PyObject *col = PyList_New(0);
    if (! col)
        return NULL;

    PyList_Append(col, _str_values_paren);

    limit = PyTuple_GET_SIZE(KosValues_ARGS(s));
    for (count = 0; count < limit; count++) {
        tmp = PyObject_Repr(PyTuple_GET_ITEM(KosValues_ARGS(s), count));
        if (! tmp) {
            Py_DECREF(col);
            return NULL;
        }
        PyList_Append(col, tmp);
        PyList_Append(col, _str_comma_space);
        Py_DECREF(tmp);
    }

    count = 0;
    if (KosValues_KWDS(s) && PyDict_Size(KosValues_KWDS(s))) {
        while (PyDict_Next(KosValues_KWDS(s), &count, &key, &value)) {
            tmp = PyObject_Repr(value);
            if (! tmp) {
                Py_DECREF(col);
                return NULL;
            }
            PyList_Append(col, key);
            PyList_Append(col, _str_equals);
            PyList_Append(col, tmp);
            PyList_Append(col, _str_comma_space);
            Py_DECREF(tmp);
        }
    }

    if (limit || count) {
        /* we'll have a trailing _str_comma_space if we added
           anything. Re-use its index for the close paren */
        Py_INCREF(_str_close_paren);
        PyList_SetItem(col, PyList_GET_SIZE(col) - 1, _str_close_paren);
    } else {
        /* otherwise, just close off as "values()" */
        PyList_Append(col, _str_close_paren);
    }

    tmp = PyUnicode_Join(_str_empty, col);
    Py_DECREF(col);

    return tmp;
}


static Py_hash_t values_hash(PyObject *self) {
    KosValues *s = (KosValues *) self;
    Py_uhash_t result = KosValues_HASHED(s), khash;
    PyObject *tmp, *frozen;

    if (result == 0) {
        result = PyObject_Hash(KosValues_ARGS(s));
        if (result == (Py_uhash_t) -1)
            return -1;

        if (KosValues_KWDS(s) && PyDict_Size(KosValues_KWDS(s))) {
            tmp = _PyDictView_New(KosValues_KWDS(s), &PyDictItems_Type);
            if (! tmp)
                return -1;

            frozen = PyFrozenSet_New(tmp);
            Py_DECREF(tmp);

            if (! frozen)
                return -1;

            khash = PyObject_Hash(frozen);
            Py_DECREF(frozen);

            if (khash == (Py_uhash_t) -1)
                return -1;

            /* I stole these magic numbers from tuplehash */
            result = (result ^ khash) * _PyHASH_MULTIPLIER;
            result += 97531UL;

            if (result == (Py_uhash_t) -1)
                result = (Py_uhash_t) -2;
        }

        KosValues_HASHED(s) = result;
    }

    return result;
}


static long values_eq(PyObject *self, PyObject *other) {
    KosValues *s = (KosValues *) self;
    long answer = 0;

    if (self == other) {
        /* identity is equality, yes */
        answer = 1;

    } else if (KosValues_CheckExact(other)) {
        KosValues *o = (KosValues *) other;

        /* when comparing two values against each other, we'll just
           compare their positionals and keywords. We'll actually do the
           keywords check first, because it has a quick NULL-check */

        if (KosValues_KWDS(s) && KosValues_KWDS(o)) {
            answer = PyObject_RichCompareBool(KosValues_KWDS(s), KosValues_KWDS(o), Py_EQ);
        } else {
            answer = (KosValues_KWDS(s) == KosValues_KWDS(o));
        }

        answer = answer && \
            PyObject_RichCompareBool(KosValues_ARGS(s), KosValues_ARGS(o), Py_EQ);

    } else if (PyTuple_CheckExact(other)) {
        /* comparing against a tuple is fine, so long as keywords either
           are NULL or empty. */

        answer = ((! KosValues_KWDS(s)) || (! PyDict_Size(KosValues_KWDS(s)))) &&   \
            PyObject_RichCompareBool(KosValues_ARGS(s), other, Py_EQ);

    } else if (PyDict_CheckExact(other)) {
        /* comparing against a dict is fine, so long as positionals is
           empty. */

        if (KosValues_KWDS(s)) {
            answer = (! PyTuple_GET_SIZE(KosValues_ARGS(s))) &&           \
                PyObject_RichCompareBool(KosValues_KWDS(s), other, Py_EQ);

        } else {
            /* we'll say a NULL keywords is equal to an empty dict */
            answer = (! PyTuple_GET_SIZE(KosValues_ARGS(s))) && \
                (! PyDict_Size(other));
        }
    }

    return answer;
}


static PyObject *values_richcomp(PyObject *self, PyObject *other, int op) {
    if (op == Py_EQ) {
        return PyBool_FromLong(values_eq(self, other));

    } else if (op == Py_NE) {
        return PyBool_FromLong(! values_eq(self, other));

    } else {
        PyErr_SetString(PyExc_TypeError, "unsupported values comparison");
        return NULL;
    }
}


static int values_bool(PyObject *self) {
    PyObject *args = KosValues_ARGS(self);
    PyObject *kwds = KosValues_KWDS(self);

    return !!((kwds && PyDict_Size(kwds)) ||
              (args && PyTuple_GET_SIZE(args)));
}


static PyObject *values_add(PyObject *left, PyObject *right) {
    KosValues *result = NULL;
    PyObject *args = NULL, *kwds = NULL, *tmp;

    if (KosValues_CheckExact(left)) {
        KosValues *s = (KosValues *) left;

        if (KosValues_CheckExact(right)) {
            KosValues *o = (KosValues *) right;

            args = PySequence_Concat(KosValues_ARGS(s), KosValues_ARGS(o));
            if (! args)
                return NULL;

            if (KosValues_KWDS(o)) {
                kwds = KosValues_KWDS(s) ? PyDict_Copy(KosValues_KWDS(s)) : PyDict_New();
                PyDict_Update(kwds, KosValues_KWDS(o));
            } else {
                kwds = KosValues_KWDS(s);
                Py_XINCREF(kwds);
            }

        } else if (PyDict_Check(right)) {
            args = KosValues_ARGS(s);
            Py_INCREF(args);

            kwds = KosValues_KWDS(s) ? PyDict_Copy(KosValues_KWDS(s)) : PyDict_New();
            PyDict_Update(kwds, right);

        } else {
            tmp = PySequence_Tuple(right);
            if (! tmp)
                return NULL;

            args = PySequence_Concat(KosValues_ARGS(s), tmp);
            Py_DECREF(tmp);

            if (! args)
                return NULL;

            kwds = KosValues_KWDS(s);
            Py_XINCREF(kwds);
        }

    } else if (KosValues_CheckExact(right)) {
        KosValues *s = (KosValues *) right;

        if (PyDict_Check(left)) {
            args = KosValues_ARGS(s);
            Py_INCREF(args);

            if (KosValues_KWDS(s)) {
                kwds = PyDict_Copy(left);
                PyDict_Update(kwds, KosValues_KWDS(s));

            } else {
                kwds = PyDict_Copy(left);
            }

        } else {
            tmp = PySequence_Tuple(left);
            if (! tmp)
                return NULL;

            args = PySequence_Concat(tmp, KosValues_ARGS(s));
            Py_DECREF(tmp);

            if (! args)
                return NULL;

            kwds = KosValues_KWDS(s);
            Py_XINCREF(kwds);
        }

    } else {
        PyErr_SetString(PyExc_TypeError, "values_add invoked with no values");
        return NULL;
    }

    result = (KosValues *) KosValues_New(args, NULL);
    if (result) {
        KosValues_KWDS(result) = kwds;  /* just to avoid another copy */
    } else {
        Py_XDECREF(kwds);
    }
    Py_DECREF(args);

    return (PyObject *) result;
}


static PyObject *values_keys(PyObject *self, PyObject *_noargs) {
    (void)_noargs;

    KosValues *s = (KosValues *) self;
    PyObject *tmp, *result = NULL;

    if (KosValues_KWDS(s)) {
        /* this is what the default keys() impl on dict does. The
           PyDict_Keys API creates a list, which we don't want to do. */
        result = _PyDictView_New(KosValues_KWDS(s), &PyDictKeys_Type);

    } else {
        /* a cheap empty iterator */
        tmp = PyTuple_New(0);
        result = PyObject_GetIter(tmp);
        Py_DECREF(tmp);
    }

    return result;
}


static PyMethodDef values_methods[] = {
    { "keys", (PyCFunction) values_keys, METH_NOARGS,
      "V.keys()" },

    { NULL, NULL, 0, NULL },
};


static PyNumberMethods values_as_number = {
    .nb_bool = (inquiry) values_bool,
    .nb_add = values_add,
};


static PySequenceMethods values_as_sequence = {
    .sq_length = values_args_length,
    .sq_item = values_args_getitem,
};


static PyMappingMethods values_as_mapping = {
    .mp_length = values_kwds_length,
    .mp_subscript = values_kwds_getitem,
};


KosEvaluatedTypeObject KosValuesType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "values",
    .tp_basicsize = sizeof(KosValues),
    .tp_itemsize = 0,

    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_methods = values_methods,
    .tp_new = values_new,
    .tp_dealloc = values_dealloc,
    .tp_traverse = values_traverse,
    .tp_clear = values_clear,

    .tp_iter = values_iter,
    .tp_hash = values_hash,
    .tp_as_number = &values_as_number,
    .tp_as_sequence = &values_as_sequence,
    .tp_as_mapping = &values_as_mapping,

    .tp_call = KosCallAdapt_CallAsync,
    .tp_repr = values_repr,
    .tp_richcompare = values_richcomp,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .ki_install = values_kos_install,
    .ki_vinstall = values_kos_vinstall,
    .ki_wants_frame = 1,
    .ki_wants_frame_fn = NULL,
    .ki_pycall = values_call,
    .ki_pyvectorcall = NULL,
  },
};


PyObject *KosValues_New(PyObject *args, PyObject *kwds) {
    KosValues *self = NULL;

    if (! args) {
        PyErr_SetString(PyExc_TypeError, "values require arguments");
        return NULL;
    }

    self = PyObject_GC_New(KosValues, KOS_EVALUATED_PYTYPE(KosValuesType));
    if (! self)
        return NULL;

    KosValues_KWDS(self) = NULL;

    KosValues_ARGS(self) = PySequence_Tuple(args);
    if (! KosValues_ARGS(self)) {
        Py_DECREF(self);
        return NULL;
    }

    if (kwds) {
        KosValues_KWDS(self) = PyDict_Copy(kwds);
        if (! KosValues_KWDS(self)) {
            Py_DECREF(self);
            return NULL;
        }
    }

    KosValues_HASHED(self) = 0;

    PyObject_GC_Track((PyObject *) self);
    return (PyObject *) self;
}


int koskari_values_init(PyObject *mod) {
    if (! mod)
        return -1;

    /* Initialize string constants */
    STR_CONST(_str_values_paren, "values(");
    STR_CONST(_str_comma_space, ", ");
    STR_CONST(_str_close_paren, ")");
    STR_CONST(_str_equals, "=");
    STR_CONST(_str_empty, "");

    if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosValuesType)))
        return -1;

    PyObject *dict = PyModule_GetDict(mod);
    PyDict_SetItemString(dict, "values",
                         (PyObject *) KOS_EVALUATED_PYTYPE(KosValuesType));

    return 0;
}


int koskari_values_clear(PyObject *mod) {
    (void)mod;

    /* Clear string constants */
    Py_CLEAR(_str_values_paren);
    Py_CLEAR(_str_comma_space);
    Py_CLEAR(_str_close_paren);
    Py_CLEAR(_str_equals);
    Py_CLEAR(_str_empty);

    return 0;
}


/* The end. */
