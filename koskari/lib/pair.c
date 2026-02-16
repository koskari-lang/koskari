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
   Cons pair and nil types

   The pair type is a mutable cons cell with head (car) and tail (cdr)
   slots. The nil singleton represents the empty list and serves as
   the base type for pairs.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"


/* === string constants === */


static PyObject *_str_close_paren = NULL;
static PyObject *_str_comma_space = NULL;
static PyObject *_str_cons_paren = NULL;
static PyObject *_str_dot_space = NULL;
static PyObject *_str_elipsis = NULL;
static PyObject *_str_empty = NULL;
static PyObject *_str_nil = NULL;
static PyObject *_str_open_paren = NULL;
static PyObject *_str_recursive = NULL;
static PyObject *_str_recursive_true = NULL;
static PyObject *_str_space = NULL;
static PyObject *_str_quote = NULL;
static PyObject *_str_esc_quote = NULL;


/* === pair free list === */


#define PAIR_MAX_FREE 256

static KosPair *pair_free_list = NULL;
static int pair_free_count = 0;


/* === helper macros === */


#define Py_ASSIGN(dest, value) {                \
    Py_XDECREF(dest);                           \
    dest = value;                               \
    Py_XINCREF(dest);                           \
  }


/* trashcan macros for deep recursion protection */
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION < 8
#  define CPy_TRASHCAN_BEGIN(op, dealloc) Py_TRASHCAN_SAFE_BEGIN(op)
#  define CPy_TRASHCAN_END(op) Py_TRASHCAN_SAFE_END(op)
#else
#  define CPy_TRASHCAN_BEGIN(op, dealloc) Py_TRASHCAN_BEGIN(op, dealloc)
#  define CPy_TRASHCAN_END(op) Py_TRASHCAN_END
#endif


/* === PairIteratorType === */


typedef struct {
  PyObject_HEAD
  PyObject *pair;
  int index;
} KosPairIterator;


static void piter_dealloc(PyObject *self) {
  Py_CLEAR(((KosPairIterator *) self)->pair);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *piter_iternext(PyObject *self) {
  KosPairIterator *s = (KosPairIterator *) self;
  PyObject *pair = s->pair;
  PyObject *result = NULL;

  if (pair) {
    switch(s->index) {
    case 0:
      result = KosPair_CAR(pair);
      Py_INCREF(result);
      s->index = 1;
      break;

    case 1:
      result = KosPair_CDR(pair);
      Py_INCREF(result);
      s->index = 2;
      Py_CLEAR(s->pair);
      break;

    default:
      result = NULL;
    }
  }

  return result;
}


PyTypeObject KosPairIteratorType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "pair_iterator",
  sizeof(KosPairIterator),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_dealloc = piter_dealloc,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = piter_iternext,
};


/* === PairFollowerType === */


typedef struct {
  PyObject_HEAD
  PyObject *current;
  PyObject *seen;
  long just_items;
} KosPairFollower;


static void pfoll_dealloc(PyObject *self) {
  Py_CLEAR(((KosPairFollower *) self)->current);
  Py_CLEAR(((KosPairFollower *) self)->seen);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *pfoll_iternext(PyObject *self) {
  KosPairFollower *s = (KosPairFollower *) self;
  PyObject *current;
  PyObject *curr_id;
  PyObject *result = NULL;

  current = s->current;

  if (! current) {
    /* then it's done */
    return NULL;
  }

  if (s->seen) {
    /* we're set up to keep a record of what pairs we've already
       seen, to prevent recursion */
    curr_id = PyLong_FromVoidPtr(current);

    if (PySet_Contains(s->seen, curr_id)) {
      /* if we've already seen this one, we're done */
      Py_DECREF(curr_id);
      Py_CLEAR(s->current);
      return NULL;

    } else {
      /* otherwise mark it as seen, and continue */
      PySet_Add(s->seen, curr_id);
      Py_DECREF(curr_id);
    }
  }

  /* get ready for the next */
  if (KosPair_CheckExact(current)) {
    s->current = KosPair_CDR(current);
    Py_INCREF(s->current);

  } else {
    s->current = NULL;
  }

  /* at this point, we've stolen the old s->current ref as current,
     and s->current has been modified to point to something else */

  if (s->just_items) {
    /* if we're in items mode, then we want the CAR if current is a
       pair, otherwise if current is non-nil we return it. */
    if (KosPair_CheckExact(current)) {
      result = KosPair_CAR(current);
      Py_INCREF(result);
      Py_DECREF(current);
    } else if (KosNil_Check(current)) {
      result = NULL;
      Py_DECREF(current);
    } else {
      result = current;
    }
  } else {
    result = current;
  }

  return result;
}


PyTypeObject KosPairFollowerType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "pair_follower",
  sizeof(KosPairFollower),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_dealloc = pfoll_dealloc,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = pfoll_iternext,
};


/* === pair === */


static PyObject *pair_new(PyTypeObject *type,
                          PyObject *args, PyObject *kwds) {

  PyObject *head = NULL;
  PyObject *tail = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "pair takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "OO:pair", &head, &tail))
    return NULL;

  return KosPair_New(head, tail);
}


static Py_ssize_t pair_len(PyObject *self) {
  return 2;
}


static PyObject *pair_getitem(PyObject *self, Py_ssize_t index) {
  PyObject *result = NULL;

  switch(index) {
  case 0:
    result = KosPair_CAR(self);
    Py_XINCREF(result);
    break;

  case 1:
    result = KosPair_CDR(self);
    Py_XINCREF(result);
    break;

  default:
    PyErr_SetString(PyExc_IndexError, "pair index out of range");
  }

  return result;
}


static int pair_setitem(PyObject *self, Py_ssize_t index, PyObject *val) {
  int result = 0;

  switch(index) {
  case 0:
    KosPair_SETCAR(self, val);
    break;

  case 1:
    KosPair_SETCDR(self, val);
    break;

  default:
    PyErr_SetString(PyExc_IndexError, "pair index out of range");
    result = -1;
  }

  return result;
}


static PyObject *pair_iter(PyObject *self) {
  KosPairIterator *i = PyObject_New(KosPairIterator, &KosPairIteratorType);
  if(! i)
    return NULL;

  Py_INCREF(self);
  i->pair = self;
  i->index = 0;

  return (PyObject *) i;
}


static PyObject *pair_repr(PyObject *self) {
  PyObject *tmp = NULL;
  PyObject *col = PyList_New(0);
  PyObject *found = PyDict_New();
  size_t index = 0;

  PyObject *rest = self;
  PyObject *rest_id;

  PyList_Append(col, _str_cons_paren);

  while (rest->ob_type == &KosPairType) {
    rest_id = PyLong_FromVoidPtr(rest);

    if (PyDict_Contains(found, rest_id)) {
      /* recursive pair detected */
      PyList_Append(col, _str_recursive_true);

      if (rest != self) {
        tmp = PyDict_GetItem(found, rest_id);
        PyList_Insert(col, PyLong_AsSize_t(tmp) - 1, _str_cons_paren);
        PyList_Append(col, _str_close_paren);
      }
      Py_DECREF(rest_id);
      rest = NULL;
      break;

    } else {
      index += 2;

      tmp = PyLong_FromSize_t(index);
      PyDict_SetItem(found, rest_id, tmp);
      Py_DECREF(tmp);

      tmp = PyObject_Repr(KosPair_CAR(rest));
      PyList_Append(col, tmp);
      PyList_Append(col, _str_comma_space);
      Py_DECREF(tmp);

      rest = KosPair_CDR(rest);
      Py_DECREF(rest_id);
    }
  }

  if (rest) {
    tmp = PyObject_Repr(rest);
    PyList_Append(col, tmp);
    Py_DECREF(tmp);
  }

  PyList_Append(col, _str_close_paren);

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_DECREF(found);

  return tmp;
}


static PyObject *sib_quoted(PyObject *u) {
  /* quote a string for display in str() output */

  PyObject *escaped = PyUnicode_Replace(u, _str_quote, _str_esc_quote, -1);
  PyObject *result = PyUnicode_FromFormat("\"%U\"", escaped);
  Py_DECREF(escaped);
  return result;
}


static PyObject *pair_str(PyObject *self) {
  PyObject *tmp = NULL;
  PyObject *col = PyList_New(0);
  PyObject *found = PyDict_New();
  size_t index = 0;

  PyObject *rest = self;
  PyObject *rest_id;

  PyList_Append(col, _str_open_paren);

  for (rest = self; KosPair_CheckExact(rest); rest = KosPair_CDR(rest)) {
    rest_id = PyLong_FromVoidPtr(rest);

    if (PyDict_Contains(found, rest_id)) {
      /* recursive pair detected */
      PyList_Append(col, _str_elipsis);
      PyList_Append(col, _str_close_paren);

      if (rest != self) {
        tmp = PyDict_GetItem(found, rest_id);
        PyList_Insert(col, PyLong_AsSize_t(tmp) - 1, _str_open_paren);
        PyList_Insert(col, PyLong_AsSize_t(tmp) - 1, _str_dot_space);
        PyList_Append(col, _str_close_paren);
      }
      Py_DECREF(rest_id);

      /* set rest to NULL so we don't try to close it like we would a
         normal proper or improper list */
      rest = NULL;
      break;

    } else {
      index += 2;

      tmp = PyLong_FromSize_t(index);
      PyDict_SetItem(found, rest_id, tmp);
      Py_DECREF(tmp);
      Py_DECREF(rest_id);

      tmp = KosPair_CAR(rest);
      if (PyUnicode_CheckExact(tmp)) {
        tmp = sib_quoted(tmp);
      } else {
        tmp = PyObject_Str(tmp);
      }

      PyList_Append(col, tmp);
      Py_DECREF(tmp);

      PyList_Append(col, _str_space);
    }
  }

  if(KosNil_Check(rest)) {
    /* end of a proper list */

    Py_INCREF(_str_close_paren);
    PyList_SetItem(col, index, _str_close_paren);

  } else if (rest) {
    /* end of an improper list */

    tmp = rest;
    if (PyUnicode_CheckExact(tmp)) {
      tmp = sib_quoted(tmp);
    } else {
      tmp = PyObject_Str(tmp);
    }
    PyList_Append(col, _str_dot_space);
    PyList_Append(col, tmp);
    PyList_Append(col, _str_close_paren);
    Py_DECREF(tmp);
  }

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_DECREF(found);

  return tmp;
}


static long pair_eq(PyObject *left, PyObject *right) {
  PyObject *seen = NULL;
  PyObject *left_id, *right_id;
  long answer = 1;
  void *left_seen, *right_seen;

  if (left == right)
    return 1;

  seen = PyDict_New();

  while (left != right) {
    if (KosNil_Check(left) || KosNil_Check(right)) {
      /* one of our comparisons is a nil, and nil can only equal
         itself, and left and right aren't the same object, so
         therefore... nope */
      answer = 0;
      break;

    } else if (left->ob_type != &KosPairType) {
      /* left is not a pair, use its comparison function instead of
         ours */
      answer = PyObject_RichCompareBool(left, right, Py_EQ);
      break;

    } else if (right->ob_type != &KosPairType) {
      /* left is a pair, but right is not, and pairs can only be
         equivalent to other pairs, so nope. */
      answer = 0;
      break;
    }

    /* if we reach this point, left and right are both KosPairType,
       and they do not refer to the same memory space, so we can test
       their CAR equivs and their CDR equivs */

    left_id = PyLong_FromVoidPtr(left);
    right_id = PyLong_FromVoidPtr(right);

    left_seen = PyDict_GetItem(seen, left_id);
    right_seen = PyDict_GetItem(seen, right_id);

    if ((left_seen && PyLong_AsVoidPtr(left_seen) == right) ||
        (right_seen && PyLong_AsVoidPtr(right_seen) == left)) {

      /* we've already compared these two against one another */
      Py_DECREF(left_id);
      Py_DECREF(right_id);
      break;

    } else if (! PyObject_RichCompareBool(KosPair_CAR(left),
                                          KosPair_CAR(right), Py_EQ)) {
      /* haven't been compared before, but they're not equal */
      answer = 0;
      Py_DECREF(left_id);
      Py_DECREF(right_id);
      break;

    } else {
      /* equal, so make note that we've compared these to each other */
      PyDict_SetItem(seen, left_id, right_id);
      PyDict_SetItem(seen, right_id, left_id);
      Py_DECREF(left_id);
      Py_DECREF(right_id);
    }

    left = KosPair_CDR(left);
    right = KosPair_CDR(right);
  }

  Py_DECREF(seen);
  return answer;
}


static PyObject *pair_richcomp(PyObject *self, PyObject *other, int op) {
  long eq = pair_eq(self, other);
  if (eq < 0)
    return NULL;

  if (op == Py_EQ) {
    return PyBool_FromLong(eq);

  } else if (op == Py_NE) {
    return PyBool_FromLong(! eq);

  } else {
    PyErr_SetString(PyExc_TypeError, "unsupported pair comparison");
    return NULL;
  }
}


static void pair_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  CPy_TRASHCAN_BEGIN(self, pair_dealloc);

  Py_CLEAR(KosPair_CAR(self));
  Py_CLEAR(KosPair_CDR(self));
  Py_CLEAR(KosPair_POSITION(self));

  if (pair_free_count < PAIR_MAX_FREE) {
    KosPair_CDR(self) = (PyObject *) pair_free_list;
    pair_free_list = (KosPair *) self;
    pair_free_count++;

  } else {
    PyObject_GC_Del(self);
  }

  CPy_TRASHCAN_END(self);
}


static int pair_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosPair_CAR(self));
  Py_VISIT(KosPair_CDR(self));
  Py_VISIT(KosPair_POSITION(self));
  return 0;
}


static int pair_clear(PyObject *self) {
  Py_CLEAR(KosPair_CAR(self));
  Py_CLEAR(KosPair_CDR(self));
  Py_CLEAR(KosPair_POSITION(self));
  return 0;
}


static PyObject *pair_copy(PyObject *self, PyObject *_noargs) {
  PyObject *result = NULL;
  PyObject *tmp = NULL;
  PyObject *last = NULL;

  if (KosNil_Check(self)) {
    Py_INCREF(self);
    return self;
  }

  /* records of the IDs of originals to instances of the new copies */
  PyObject *seen = PyDict_New();

  for (; KosPair_CheckExact(self); self = KosPair_CDR(self)) {

    PyObject *self_id = PyLong_FromVoidPtr(self);

    tmp = PyDict_GetItem(seen, self_id);
    if (tmp) {
      /* we've seen this one before, so we're recursive. Make the new
         copy recursive as well and break */
      Py_DECREF(self_id);
      KosPair_SETCDR(last, tmp);
      last = NULL; /* this signals that we made a recursive link */
      break;
    }

    /* make a new pair, associate it with current ID */
    tmp = KosPair_New(KosPair_CAR(self), KosNil);
    if (! tmp) {
      Py_DECREF(seen);
      Py_XDECREF(result);
      return NULL;
    }
    Py_ASSIGN(KosPair_POSITION(tmp), KosPair_POSITION(self));

    PyDict_SetItem(seen, self_id, tmp);
    Py_DECREF(self_id);

    /* if this is our first pair copied, it's the result */
    if (! result) {
      result = tmp;
      Py_INCREF(result);
    }

    /* if we have a previous, assign this new pair to the prev's CDR
       slot. then decref it and make current into the new prev */
    if (last) {
      KosPair_SETCDR(last, tmp);
    }
    last = tmp;
  }

  /* done with loop, don't need the seen dict anymore. */
  Py_DECREF(seen);

  /* either nil or a non-pair leftover is in self, otherwise last
     would have been cleared */
  if (last)
    KosPair_SETCDR(last, self);

  if (! result)
    PyErr_SetString(PyExc_TypeError, "expected pair");

  return result;
}


static PyObject *pair_length(PyObject *self, PyObject *_noargs) {
  PyObject *seen;
  PyObject *tmp;
  long length = 0;

  if (KosNil_Check(self)) {
    return PyLong_FromLong(length);
  }

  seen = PySet_New(NULL);

  for(; KosPair_CheckExact(self); self = KosPair_CDR(self)) {

    tmp = PyLong_FromVoidPtr(self);

    if (PySet_Contains(seen, tmp)) {
      Py_DECREF(tmp);
      self = NULL;
      break;

    } else {
      PySet_Add(seen, tmp);
      Py_DECREF(tmp);
      length++;
    }
  }

  if (self && ! KosNil_Check(self))
    length++;

  Py_DECREF(seen);
  return PyLong_FromLong(length);
}


static PyObject *pair_follow(PyObject *self, PyObject *_noargs) {
  KosPairFollower *i = PyObject_New(KosPairFollower, &KosPairFollowerType);
  if(! i)
    return NULL;

  Py_INCREF(self);
  i->current = self;
  i->seen = PySet_New(NULL);
  i->just_items = 0;

  return (PyObject *) i;
}


static PyObject *pair_unpack(PyObject *self, PyObject *_noargs) {
  KosPairFollower *i = PyObject_New(KosPairFollower, &KosPairFollowerType);
  if(! i)
    return NULL;

  Py_INCREF(self);
  i->current = self;
  i->seen = PySet_New(NULL);
  i->just_items = 1;

  return (PyObject *) i;
}


PyObject *KosPair_Unpack(PyObject *pair) {
  return pair_unpack(pair, NULL);
}


long KosPair_IsProper(PyObject *self) {
  if (KosNil_Check(self))
    return 1;

  PyObject *seen = PySet_New(NULL);
  PyObject *pair_id;
  long result = 0;

  for ( ; KosPair_CheckExact(self); self = KosPair_CDR(self)) {
    pair_id = PyLong_FromVoidPtr(self);

    if (PySet_Contains(seen, pair_id)) {
      /* seen it, therefore recursive */
      Py_DECREF(pair_id);
      result = 1;
      break;

    } else {
      PySet_Add(seen, pair_id);
      Py_DECREF(pair_id);
    }
  }

  /* it's either recursive and thus proper, or the last item needs to
     have been a nil, or it's improper */
  Py_DECREF(seen);
  return result || KosNil_Check(self);
}


static PyObject *pair_is_proper(PyObject *self, PyObject *_noargs) {
  return PyBool_FromLong(KosPair_IsProper(self));
}


long KosPair_IsRecursive(PyObject *self) {
  if (KosNil_Check(self))
    return 0;

  PyObject *seen = PySet_New(NULL);
  PyObject *pair_id;
  long result = 0;

  for ( ; KosPair_CheckExact(self); self = KosPair_CDR(self)) {
    pair_id = PyLong_FromVoidPtr(self);

    if (PySet_Contains(seen, pair_id)) {
      /* seen it, therefore recursive */
      Py_DECREF(pair_id);
      result = 1;
      break;

    } else {
      PySet_Add(seen, pair_id);
      Py_DECREF(pair_id);
    }
  }

  Py_DECREF(seen);
  return result;
}


static PyObject *pair_is_recursive(PyObject *self, PyObject *_noargs) {
  return PyBool_FromLong(KosPair_IsRecursive(self));
}


static void pwalk_setpos(PyObject *pair, PyObject *seen, PyObject *pos) {
  PyObject *pair_id;
  KosPair *sp;

  for (; KosPair_CheckExact(pair); pair = KosPair_CDR(pair)) {
    sp = (KosPair *) pair;

    pair_id = PyLong_FromVoidPtr(pair);

    if (PySet_Contains(seen, pair_id)) {
      Py_DECREF(pair_id);
      break;

    } else {
      PySet_Add(seen, pair_id);
      Py_DECREF(pair_id);
    }

    Py_ASSIGN(KosPair_POSITION(sp), pos);

    if (KosPair_CheckExact(KosPair_CAR(sp))) {
      pwalk_setpos(KosPair_CAR(sp), seen, pos);
    }
  }
}


static void pwalk_fillpos(PyObject *pair, PyObject *seen, PyObject *pos) {
  PyObject *pair_id;
  KosPair *sp;

  for (; KosPair_CheckExact(pair); pair = KosPair_CDR(pair)) {
    sp = (KosPair *) pair;

    pair_id = PyLong_FromVoidPtr(pair);

    if (PySet_Contains(seen, pair_id)) {
      Py_DECREF(pair_id);
      break;

    } else {
      PySet_Add(seen, pair_id);
      Py_DECREF(pair_id);
    }

    if (KosPair_POSITION(sp)) {
      pos = KosPair_POSITION(sp);

    } else {
      Py_INCREF(pos);
      KosPair_POSITION(sp) = pos;
    }

    if (KosPair_CheckExact(KosPair_CAR(sp))) {
      pwalk_fillpos(KosPair_CAR(sp), seen, pos);
    }
  }
}


static PyObject *pair_clear_position(PyObject *self,
                                     PyObject *args, PyObject *kwds) {

  static char *keywords[] = { "follow", NULL };
  int follow = 0;

  if (KosNil_Check(self))
    Py_RETURN_NONE;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "|p", keywords, &follow))
    return NULL;

  if (follow) {
    PyObject *seen = PySet_New(NULL);
    pwalk_setpos(self, seen, NULL);
    Py_DECREF(seen);

  } else {
    Py_CLEAR(KosPair_POSITION(self));
  }

  Py_RETURN_NONE;
}


static PyObject *pair_set_position(PyObject *self,
                                   PyObject *args, PyObject *kwds) {

  static char *keywords[] = { "position", "follow", NULL };
  PyObject *position = NULL;
  int follow = 0;

  if (KosNil_Check(self))
    Py_RETURN_NONE;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|p", keywords,
                                    &position, &follow))
    return NULL;

  if (follow) {
    PyObject *seen = PySet_New(NULL);
    pwalk_setpos(self, seen, position);
    Py_DECREF(seen);

  } else {
    Py_ASSIGN(KosPair_POSITION(self), position);
  }

  Py_RETURN_NONE;
}


static PyObject *pair_fill_position(PyObject *self,
                                    PyObject *args, PyObject *kwds) {

  static char *keywords[] = { "position", "follow", NULL };
  PyObject *position = NULL;
  int follow = 0;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|p", keywords,
                                    &position, &follow))
    return NULL;

  if (follow) {
    PyObject *seen = PySet_New(NULL);
    pwalk_fillpos(self, seen, position);
    Py_DECREF(seen);

  } else {
    if (! KosPair_POSITION(self)) {
      Py_INCREF(position);
      KosPair_POSITION(self) = position;
    }
  }

  Py_RETURN_NONE;
}


static PyObject *pair_get_position(PyObject *self, PyObject *_noargs) {
  PyObject *position = KosPair_POSITION(self);

  if (position) {
    Py_INCREF(position);
    return position;

  } else {
    Py_RETURN_NONE;
  }
}


static PyMethodDef pair_methods[] = {
  { "__copy__", (PyCFunction) pair_copy, METH_NOARGS,
    "P.__copy__()" },

  { "length", (PyCFunction) pair_length, METH_NOARGS,
    "P.length()" },

  { "follow", (PyCFunction) pair_follow, METH_NOARGS,
    "P.follow()" },

  { "unpack", (PyCFunction) pair_unpack, METH_NOARGS,
    "P.unpack()" },

  { "is_proper", (PyCFunction) pair_is_proper, METH_NOARGS,
    "P.is_proper()" },

  { "is_recursive", (PyCFunction) pair_is_recursive, METH_NOARGS,
    "P.is_recursive()" },

  { "clear_position", (PyCFunction) pair_clear_position,
    METH_VARARGS|METH_KEYWORDS,
    "P.clear_position(follow=True)" },

  { "set_position", (PyCFunction) pair_set_position,
    METH_VARARGS|METH_KEYWORDS,
    "P.set_position(position, follow=True)" },

  { "fill_position", (PyCFunction) pair_fill_position,
    METH_VARARGS|METH_KEYWORDS,
    "P.fill_position(position, follow=True)" },

  { "get_position", (PyCFunction) pair_get_position, METH_NOARGS,
    "P.get_position()" },

  { NULL, NULL, 0, NULL },
};


static PySequenceMethods pair_as_sequence = {
  .sq_length = pair_len,
  .sq_item = pair_getitem,
  .sq_ass_item = pair_setitem,
};


PyTypeObject KosPairType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "pair",
  sizeof(KosPair),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,
  .tp_methods = pair_methods,
  .tp_new = pair_new,
  .tp_dealloc = pair_dealloc,
  .tp_traverse = pair_traverse,
  .tp_clear = pair_clear,

  .tp_iter = pair_iter,
  .tp_as_sequence = &pair_as_sequence,

  .tp_repr = pair_repr,
  .tp_str = pair_str,
  .tp_richcompare = pair_richcomp,
};


PyObject *KosPair_New(PyObject *head, PyObject *tail) {
  KosPair *self = NULL;

  if (! (head && tail)) {
    PyErr_SetString(PyExc_TypeError, "pair requires a head and a tail");
    return NULL;
  }

  if (pair_free_list) {
    self = pair_free_list;
    pair_free_list = (KosPair *) KosPair_CDR(self);
    pair_free_count--;
    Py_INCREF(self);

  } else {
    self = PyObject_GC_New(KosPair, &KosPairType);
    if (! self)
      return NULL;
  }

  KosPair_POSITION(self) = NULL;

  Py_INCREF(head);
  self->head = head;

  Py_INCREF(tail);
  self->tail = tail;

  PyObject_GC_Track((PyObject *) self);
  return (PyObject *) self;
}


/* === NilType === */


static PyObject *nil_new(PyTypeObject *type,
                         PyObject *args, PyObject *kwds) {

  if (PyTuple_Size(args) || (kwds && PyDict_Size(kwds))) {
    PyErr_SetString(PyExc_TypeError, "NilType takes no arguments");
    return NULL;
  }

  Py_INCREF(KosNil);
  return KosNil;
}


static void nil_dealloc(PyObject *self) {
  ;  /* nil is statically allocated, do not free it */
}


static PyObject *nil_repr(PyObject *self) {
  Py_INCREF(_str_nil);
  return _str_nil;
}


static PyObject *nil_iter(PyObject *self) {
  KosPairIterator *i = PyObject_New(KosPairIterator, &KosPairIteratorType);
  if(! i)
    return NULL;

  i->index = 0;
  i->pair = NULL;

  return (PyObject *) i;
}


static Py_ssize_t nil_length(PyObject *self) {
  return 0;
}


static PyObject *nil_getitem(PyObject *self, Py_ssize_t index) {
  PyErr_SetString(PyExc_IndexError, "nil has no items");
  return NULL;
}


static int nil_setitem(PyObject *self, Py_ssize_t index, PyObject *val) {
  PyErr_SetString(PyExc_IndexError, "nil has no items");
  return -1;
}


static PySequenceMethods nil_as_sequence = {
  .sq_length = nil_length,
  .sq_item = nil_getitem,
  .sq_ass_item = nil_setitem,
};


static int nil_bool(PyObject *v) {
  return 0;
}


static PyNumberMethods nil_as_number = {
  .nb_bool = (inquiry) nil_bool,
};


PyTypeObject KosNilType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "NilType",
  sizeof(KosPair),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_base = &KosPairType,
  .tp_new = nil_new,
  .tp_dealloc = nil_dealloc,

  .tp_traverse = NULL,
  .tp_clear = NULL,

  .tp_repr = nil_repr,
  .tp_str = nil_repr,
  .tp_iter = nil_iter,
  .tp_as_number = &nil_as_number,
  .tp_as_sequence = &nil_as_sequence,
};


KosPair _KosNil = {
  PyObject_HEAD_INIT(&KosNilType)
  .head = NULL,
  .tail = NULL,
  .position = NULL,
};


PyObject *KosPair_Cons(PyObject *members, int recursive) {
  PyObject *seq = PySequence_Fast(members,
                                  "cons members object must be a sequence");
  if(! seq) {
    return NULL;
  }

  int count = PySequence_Fast_GET_SIZE(seq);
  if (! count) {
    Py_DECREF(seq);
    Py_INCREF(KosNil);
    return KosNil;
  }

  PyObject *result = KosPair_New(PySequence_Fast_GET_ITEM(seq, 0), KosNil);
  if (! result) {
    Py_DECREF(seq);
    return NULL;
  }

  if (count == 1) {
    if (recursive) {
      KosPair_SETCDR(result, result);
    }

  } else {
    PyObject *work = recursive? result: PySequence_Fast_GET_ITEM(seq, --count);
    Py_INCREF(work);

    while (--count) {
      PyObject *tmp = KosPair_New(PySequence_Fast_GET_ITEM(seq, count), work);
      Py_DECREF(work);
      if (! tmp) {
        Py_DECREF(seq);
        return NULL;
      }
      work = tmp;
    }

    KosPair_SETCDR(result, work);
    Py_DECREF(work);
  }

  Py_DECREF(seq);
  return result;
}


/* === module functions === */


static PyObject *m_cons(PyObject *mod, PyObject *args, PyObject *kwds) {
  int recursive = 0;

  if (kwds) {
    PyObject *tmp = PyDict_GetItem(kwds, _str_recursive);
    /* tmp is a borrowed ref */

    if (tmp) {
      if (PyDict_Size(kwds) > 1) {
        PyErr_SetString(PyExc_TypeError,
                        "cons accepts one keyword argument: recursive");
        return NULL;
      }

      recursive = PyObject_IsTrue(tmp);
      if (recursive < 0)
        return NULL;

    } else {
      if (PyDict_Size(kwds) > 0) {
        PyErr_SetString(PyExc_TypeError,
                        "cons accepts one keyword argument: recursive");
        return NULL;
      }
    }
  }

  return KosPair_Cons(args, recursive);
}


static PyObject *m_car(PyObject *mod, PyObject *pair) {
  PyObject *result = NULL;

  if (! KosPair_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "car argument must be pair");

  } else if (KosNil_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot get car of nil");

  } else {
    result = KosPair_CAR(pair);
    Py_INCREF(result);
  }

  return result;
}


static PyObject *m_cdr(PyObject *mod, PyObject *pair) {
  PyObject *result = NULL;

  if (! KosPair_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "cdr argument must be pair");

  } else if (KosNil_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot get cdr of nil");

  } else {
    result = KosPair_CDR(pair);
    Py_INCREF(result);
  }

  return result;
}


static PyObject *m_setcar(PyObject *mod, PyObject *args) {
  PyObject *pair = NULL;
  PyObject *val = NULL;

  if (! PyArg_ParseTuple(args, "O!O", &KosPairType, &pair, &val)) {
    return NULL;

  } else if (KosNil_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot set car of nil");
    return NULL;

  } else {
    KosPair_SETCAR(pair, val);
    Py_RETURN_NONE;
  }
}


static PyObject *m_setcdr(PyObject *mod, PyObject *args) {
  PyObject *pair = NULL;
  PyObject *val = NULL;

  if (! PyArg_ParseTuple(args, "O!O", &KosPairType, &pair, &val)) {
    return NULL;

  } else if (KosNil_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot set cdr of nil");
    return NULL;

  } else {
    KosPair_SETCDR(pair, val);
    Py_RETURN_NONE;
  }
}


static PyObject *m_build_unpack_pair(PyObject *mod, PyObject *seqs) {
  Py_ssize_t count = seqs? PyTuple_GET_SIZE(seqs): 0;
  Py_ssize_t index = 0;
  PyObject *tmp;

  if (! count) {
    Py_INCREF(KosNil);
    return KosNil;
  }

  PyObject *coll = PyList_New(0);

  while (index < count) {
    PyObject *item = PyTuple_GET_ITEM(seqs, index++);
    PyObject *work;

    if (KosNil_Check(item)) {
      /* skip nil */
      continue;

    } else if (KosPair_CheckExact(item)) {
      work = pair_unpack(item, NULL);

    } else {
      work = PyObject_GetIter(item);
    }

    if (! work) {
      Py_DECREF(coll);
      return NULL;
    }

    tmp = _PyList_Extend((PyListObject *) coll, work);
    Py_CLEAR(work);

    if (! tmp) {
      Py_DECREF(coll);
      return NULL;

    } else {
      Py_CLEAR(tmp);
    }
  }

  if (! PyList_GET_SIZE(coll)) {
    /* The collection didn't net any actual elements, so let's skip
       out early and just return nil */

    Py_DECREF(coll);
    Py_INCREF(KosNil);
    return KosNil;
  }

  tmp = PyTuple_GET_ITEM(seqs, count - 1);
  if (KosPair_IsProper(tmp)) {
    if(PyList_Append(coll, KosNil)) {
      Py_DECREF(coll);
      return NULL;
    }
  }

  PyObject *result = KosPair_Cons(coll, 0);
  Py_DECREF(coll);
  return result;
}


static PyMethodDef pair_mod_methods[] = {

  { "cons", (PyCFunction) m_cons, METH_VARARGS|METH_KEYWORDS,
    "cons(head, *tail, recursive=False) -> new pair\n"
    "If no tail is specified, and recursive is False (the default),\n"
    "then a nil will be presumed." },

  { "car", m_car, METH_O,
    "car(P) -> object\n"
    "Returns the head element of a koskari pair instance P."},

  { "cdr", m_cdr, METH_O,
    "cdr(P) -> object\n"
    "Returns the tail element of a koskari pair instance P." },

  { "setcar", m_setcar, METH_VARARGS,
    "setcar(P, obj) -> None\n"
    "Assigns the head element of a koskari pair instance P to obj." },

  { "setcdr", m_setcdr, METH_VARARGS,
    "setcdr(P, obj) -> None\n"
    "Assigns the tail element of a koskari pair instance P to obj." },

  { "build_unpack_pair", (PyCFunction) m_build_unpack_pair, METH_VARARGS,
    "build_unpack_pair(*pair_or_seq) -> new pair\n"
    "Creates a new koskari pair list from a collection of pair or\n"
    "non-pair sequences." },

  { NULL, NULL, 0, NULL },
};


/* === unit lifecycle === */


int koskari_pair_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* initialize string constants */
  STR_CONST(_str_close_paren, ")");
  STR_CONST(_str_comma_space, ", ");
  STR_CONST(_str_cons_paren, "cons(");
  STR_CONST(_str_dot_space, ". ");
  STR_CONST(_str_elipsis, "...");
  STR_CONST(_str_empty, "");
  STR_CONST(_str_nil, "nil");
  STR_CONST(_str_open_paren, "(");
  STR_CONST(_str_recursive, "recursive");
  STR_CONST(_str_recursive_true, "recursive=True");
  STR_CONST(_str_space, " ");
  STR_CONST(_str_quote, "\"");
  STR_CONST(_str_esc_quote, "\\\"");

  /* initialize types */
  if (PyType_Ready(&KosPairType))
    return -1;

  if (PyType_Ready(&KosPairIteratorType))
    return -1;

  if (PyType_Ready(&KosPairFollowerType))
    return -1;

  if (PyType_Ready(&KosNilType))
    return -1;

  /* add types and nil to module dict */
  if (PyDict_SetItemString(dict, "pair", (PyObject *) &KosPairType))
    return -1;

  if (PyDict_SetItemString(dict, "nil", KosNil))
    return -1;

  /* add module functions */
  if (PyModule_AddFunctions(mod, pair_mod_methods))
    return -1;

  return 0;
}


int koskari_pair_clear(PyObject *mod) {

  /* clear free list */
  while (pair_free_list) {
    KosPair *tmp = pair_free_list;
    pair_free_list = (KosPair *) KosPair_CDR(tmp);
    PyObject_GC_Del(tmp);
  }
  pair_free_count = 0;

  /* clear string constants */
  Py_CLEAR(_str_close_paren);
  Py_CLEAR(_str_comma_space);
  Py_CLEAR(_str_cons_paren);
  Py_CLEAR(_str_dot_space);
  Py_CLEAR(_str_elipsis);
  Py_CLEAR(_str_empty);
  Py_CLEAR(_str_nil);
  Py_CLEAR(_str_open_paren);
  Py_CLEAR(_str_recursive);
  Py_CLEAR(_str_recursive_true);
  Py_CLEAR(_str_space);

  Py_CLEAR(_str_quote);
  Py_CLEAR(_str_esc_quote);

  return 0;
}


// The end.
