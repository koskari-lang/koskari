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
   slots. The nil singleton represents the empty list and is a
   subtype of pair.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "tracing.h"


/* === string constants === */


static PyObject *_str_close_paren = NULL;
static PyObject *_str_comma_space = NULL;
static PyObject *_str_cons_paren = NULL;
static PyObject *_str_dot_space = NULL;
static PyObject *_str_elipsis = NULL;
static PyObject *_str_empty = NULL;
static PyObject *_str_nil = NULL;
static PyObject *_str_open_paren = NULL;
static PyObject *_str_recursive_true = NULL;
static PyObject *_str_space = NULL;
static PyObject *_str_open_bracket = NULL;
static PyObject *_str_close_bracket = NULL;
static PyObject *_str_bracket_cons_paren = NULL;
static PyObject *_str_empty_paren = NULL;
static PyObject *_str_empty_bracket = NULL;
static PyObject *_str_bracket_nil = NULL;


/* === pair free list === */


#define PAIR_MAX_FREE KOS_FREELIST_CAP(256)

static KosPair *pair_free_list = NULL;
static int pair_free_count = 0;


/* === helper macros === */


/* Store value into dest, taking a reference to it before the old
   value is released, so a finalizer the release runs sees the slot
   already holding the new value. */
#define Py_ASSIGN(dest, value) {                \
    PyObject *_kos_new = (value);               \
    Py_XINCREF(_kos_new);                       \
    Py_XSETREF(dest, _kos_new);                 \
  }


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
  if (KosPair_IsCell(current)) {
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
    if (KosPair_IsCell(current)) {
      result = KosPair_CAR(current);
      Py_INCREF(result);
      Py_DECREF(current);
    } else if (KosNilP(current)) {
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


static Py_ssize_t pair_len(const PyObject *self) {
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


static int pair_write_car(PyObject *pair, PyObject *val);
static int pair_write_cdr(PyObject *pair, PyObject *val);


static int pair_setitem(PyObject *self, Py_ssize_t index, PyObject *val) {

  switch(index) {
  case 0:
    return pair_write_car(self, val);

  case 1:
    return pair_write_cdr(self, val);

  default:
    PyErr_SetString(PyExc_IndexError, "pair index out of range");
    return -1;
  }
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


static PyObject *pair_repr_inner(PyObject *self, PyObject *cons_open) {
  PyObject *tmp = NULL;
  PyObject *col = PyList_New(0);
  PyObject *found = PyDict_New();
  size_t index = 0;

  PyObject *rest = self;
  PyObject *rest_id;

  if (! col || ! found) {
    Py_XDECREF(col);
    Py_XDECREF(found);
    return NULL;
  }

  PyList_Append(col, cons_open);

  /* rest is held for the whole iteration, and the car across its
     repr: an element's __repr__ can cut the spine or swap the car,
     and the walk keeps reading the cells it reached */
  Py_INCREF(rest);
  while (KosPair_IsCell(rest)) {
    rest_id = PyLong_FromVoidPtr(rest);

    if (PyDict_Contains(found, rest_id)) {
      /* recursive pair detected */
      PyList_Append(col, _str_recursive_true);

      if (rest != self) {
        tmp = PyDict_GetItem(found, rest_id);
        PyList_Insert(col, PyLong_AsSize_t(tmp) - 1, cons_open);
        PyList_Append(col, _str_close_paren);
      }
      Py_DECREF(rest_id);
      Py_CLEAR(rest);
      break;

    } else {
      PyObject *car, *next;

      index += 2;

      tmp = PyLong_FromSize_t(index);
      PyDict_SetItem(found, rest_id, tmp);
      Py_DECREF(tmp);
      Py_DECREF(rest_id);

      car = KosPair_CAR(rest);
      Py_INCREF(car);
      tmp = PyObject_Repr(car);
      Py_DECREF(car);
      if (! tmp) {
        Py_DECREF(rest);
        Py_DECREF(col);
        Py_DECREF(found);
        return NULL;
      }
      PyList_Append(col, tmp);
      PyList_Append(col, _str_comma_space);
      Py_DECREF(tmp);

      next = KosPair_CDR(rest);
      Py_INCREF(next);
      Py_SETREF(rest, next);
    }
  }

  if (rest) {
    tmp = PyObject_Repr(rest);
    Py_DECREF(rest);
    if (! tmp) {
      Py_DECREF(col);
      Py_DECREF(found);
      return NULL;
    }
    PyList_Append(col, tmp);
    Py_DECREF(tmp);
  }

  PyList_Append(col, _str_close_paren);

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_DECREF(found);

  return tmp;
}


static PyObject *pair_repr(PyObject *self) {
  return pair_repr_inner(self, _str_cons_paren);
}


/**
   The str of one element of a printed pair: a string is quoted, a
   gensym prints the symbol it spells, since str of a form is source
   and hygiene provenance is not, and anything else prints as str
   does. New reference, or NULL with an exception set.
*/
static PyObject *pair_elem_str(PyObject *elem) {
  if (PyUnicode_CheckExact(elem))
    return KosUtils_QuoteStr(elem);
  if (KosGensym_Check(elem))
    return PyObject_Str(KosGensym_BASE(elem));
  return PyObject_Str(elem);
}


/**
  Render a pair between open_s and close_s, each element through
  render: str for pair_str, KosUtils_Repr for __kos_repr__. A spine
  that loops prints its revisited cell as an ellipsis.
*/
static PyObject *pair_render_inner(
    PyObject *self,
    PyObject *open_s,
    PyObject *close_s,
    PyObject *(*render)(PyObject *)) {

  PyObject *tmp = NULL;
  PyObject *col = PyList_New(0);
  PyObject *found = PyDict_New();
  size_t index = 0;

  PyObject *rest = self;
  PyObject *rest_id;

  if (! col || ! found) {
    Py_XDECREF(col);
    Py_XDECREF(found);
    return NULL;
  }

  if (KosNilP(self)) {
    /* the empty list renders as its own open/close delimiters */
    Py_DECREF(col);
    Py_DECREF(found);
    return PyUnicode_Concat(open_s, close_s);
  }

  PyList_Append(col, open_s);

  /* rest is held for the whole iteration, and the car across its
     render: an element can cut the spine or swap the car while it
     renders, and the walk keeps reading the cells it reached */
  Py_INCREF(rest);
  while (KosPair_IsCell(rest)) {
    rest_id = PyLong_FromVoidPtr(rest);

    if (PyDict_Contains(found, rest_id)) {
      /* recursive pair detected */
      PyList_Append(col, _str_elipsis);
      PyList_Append(col, close_s);

      if (rest != self) {
        tmp = PyDict_GetItem(found, rest_id);
        PyList_Insert(col, PyLong_AsSize_t(tmp) - 1, open_s);
        PyList_Insert(col, PyLong_AsSize_t(tmp) - 1, _str_dot_space);
        PyList_Append(col, close_s);
      }
      Py_DECREF(rest_id);

      /* set rest to NULL so we don't try to close it like we would a
         normal proper or improper list */
      Py_CLEAR(rest);
      break;

    } else {
      PyObject *car, *next;

      index += 2;

      tmp = PyLong_FromSize_t(index);
      PyDict_SetItem(found, rest_id, tmp);
      Py_DECREF(tmp);
      Py_DECREF(rest_id);

      car = KosPair_CAR(rest);
      Py_INCREF(car);
      tmp = render(car);
      Py_DECREF(car);
      if (! tmp) {
        Py_DECREF(rest);
        Py_DECREF(col);
        Py_DECREF(found);
        return NULL;
      }

      PyList_Append(col, tmp);
      Py_DECREF(tmp);

      PyList_Append(col, _str_space);

      next = KosPair_CDR(rest);
      Py_INCREF(next);
      Py_SETREF(rest, next);
    }
  }

  if (rest && KosNilP(rest)) {
    /* end of a proper list */

    Py_DECREF(rest);
    Py_INCREF(close_s);
    PyList_SetItem(col, index, close_s);

  } else if (rest) {
    /* end of an improper list */

    tmp = render(rest);
    Py_DECREF(rest);
    if (! tmp) {
      Py_DECREF(col);
      Py_DECREF(found);
      return NULL;
    }
    PyList_Append(col, _str_dot_space);
    PyList_Append(col, tmp);
    PyList_Append(col, close_s);
    Py_DECREF(tmp);
  }

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_DECREF(found);

  return tmp;
}


static PyObject *pair_str(PyObject *self) {
  return pair_render_inner(
      self, _str_open_paren, _str_close_paren, pair_elem_str);
}


static PyObject *pair_kos_repr(PyObject *self, PyObject *_unused) {
  (void) _unused;
  return pair_render_inner(
      self, _str_open_paren, _str_close_paren, KosUtils_Repr);
}


static long pair_eq(PyObject *left, PyObject *right) {
  PyObject *seen = NULL;
  PyObject *left_id, *right_id, *visit;
  long answer = 1;
  int revisit;

  if (left == right)
    return 1;

  /* every (left, right) cell pair the walk compares, by address: the
     pairs are finite, so a walk over two circular spines comes back to
     one it has seen within their lengths' product and stops there,
     whatever the periods. Remembering only each cell's latest partner
     never repeats when the periods share no factor. */
  seen = PySet_New(NULL);
  if (! seen)
    return -1;

  /* both cells are held for the whole iteration, and both cars across
     their comparison: an element's __eq__ can cut either spine or swap
     a car, and the walk keeps reading the cells it reached */
  Py_INCREF(left);
  Py_INCREF(right);

  while (left != right) {
    if (KosNilP(left) && KosNilP(right)) {
      /* both sides reached an empty-list terminator (either bracket
         style); empties compare equal as list ends, so the lists
         match here regardless of which nil flavor each used */
      break;

    } else if (KosNilP(left) || KosNilP(right)) {
      /* exactly one side ended; the lists differ in length */
      answer = 0;
      break;

    } else if (! KosPair_Check(left)) {
      /* left is not a pair (nor a pair subclass), use its comparison
         function instead of ours */
      answer = PyObject_RichCompareBool(left, right, Py_EQ);
      break;

    } else if (! KosPair_Check(right)) {
      /* left is a pair, but right is not, and pairs can only be
         equivalent to other pairs, so nope. */
      answer = 0;
      break;
    }

    /* if we reach this point, left and right are both pairs (possibly
       bracket-pair subclasses), and they do not refer to the same
       memory space, so we can test
       their CAR equivs and their CDR equivs */

    left_id = PyLong_FromVoidPtr(left);
    right_id = left_id ? PyLong_FromVoidPtr(right) : NULL;
    visit = right_id ? PyTuple_Pack(2, left_id, right_id) : NULL;
    Py_XDECREF(left_id);
    Py_XDECREF(right_id);
    if (! visit) {
      answer = -1;
      break;
    }

    revisit = PySet_Contains(seen, visit);
    if (revisit < 0 || (! revisit && PySet_Add(seen, visit) < 0)) {
      Py_DECREF(visit);
      answer = -1;
      break;
    }
    Py_DECREF(visit);

    if (revisit) {
      /* we've already compared these two against one another */
      break;

    } else {
      PyObject *left_car = KosPair_CAR(left);
      PyObject *right_car = KosPair_CAR(right);
      int eq;

      Py_INCREF(left_car);
      Py_INCREF(right_car);
      eq = PyObject_RichCompareBool(left_car, right_car, Py_EQ);
      Py_DECREF(left_car);
      Py_DECREF(right_car);

      if (eq <= 0) {
        /* not equal, or the element comparison raised (-1) */
        answer = eq;
        break;
      }
    }

    {
      PyObject *next_left = KosPair_CDR(left);
      PyObject *next_right = KosPair_CDR(right);
      Py_INCREF(next_left);
      Py_INCREF(next_right);
      Py_SETREF(left, next_left);
      Py_SETREF(right, next_right);
    }
  }

  Py_DECREF(left);
  Py_DECREF(right);
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
  Py_TRASHCAN_BEGIN(self, pair_dealloc);

  Py_CLEAR(KosPair_CAR(self));
  Py_CLEAR(KosPair_CDR(self));
  Py_CLEAR(KosPair_POSITION(self));

  if (pair_free_count < PAIR_MAX_FREE) {
    KOS_TRACE_FL_RETURN("pair", 0);
    KosPair_CDR(self) = (PyObject *) pair_free_list;
    pair_free_list = (KosPair *) self;
    pair_free_count++;

  } else {
    KOS_TRACE_FL_RETURN("pair", 1);
    PyObject_GC_Del(self);
  }

  Py_TRASHCAN_END;
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

  if (KosNilP(self)) {
    Py_INCREF(self);
    return self;
  }

  /* records of the IDs of originals to instances of the new copies */
  PyObject *seen = PyDict_New();

  for (; KosPair_IsCell(self); self = KosPair_CDR(self)) {

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

    /* make a new cell of the same delimiter style as the source cell;
       the KosNil tail is a placeholder, overwritten below by the link to
       the next cell or by the terminator after the loop. */
    if (KosBracketPair_Check(self)) {
      tmp = KosBracketPair_New(KosPair_CAR(self), KosNil);
    } else {
      tmp = KosPair_New(KosPair_CAR(self), KosNil);
    }
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

    /* the seen dict and the result chain now own the new cell; drop
       the reference from creation so each cell ends with one owner. */
    last = tmp;
    Py_DECREF(tmp);
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


/**
   Floyd's walk over the spine from head. The walk only reads cdr
   links and runs no user code, so the borrowed cells stay put.

   :param head: the first cell, or any non-cell for an empty spine
   :param mu: the number of cells before the cycle, set when cyclic
   :param lambda: the number of cells in the cycle, set when cyclic
   :return: 1 when the spine loops, else 0
*/
static int pair_spine_cycle(
    PyObject *head,
    Py_ssize_t *mu,
    Py_ssize_t *lambda) {

  PyObject *slow = head;
  PyObject *fast = head;
  Py_ssize_t n;

  while (1) {
    if (! KosPair_IsCell(fast))
      return 0;
    fast = KosPair_CDR(fast);
    if (! KosPair_IsCell(fast))
      return 0;
    fast = KosPair_CDR(fast);
    slow = KosPair_CDR(slow);
    if (slow == fast)
      break;
  }

  /* from the meeting point and from head, the walks meet again where
     the cycle starts */
  slow = head;
  for (n = 0; slow != fast; n++) {
    slow = KosPair_CDR(slow);
    fast = KosPair_CDR(fast);
  }
  *mu = n;

  n = 1;
  for (fast = KosPair_CDR(slow); fast != slow; fast = KosPair_CDR(fast))
    n++;
  *lambda = n;

  return 1;
}


static PyObject *pair_length(PyObject *self, PyObject *_noargs) {
  Py_ssize_t mu, lambda;
  Py_ssize_t length = 0;

  if (pair_spine_cycle(self, &mu, &lambda))
    return PyLong_FromSsize_t(mu + lambda);

  for (; KosPair_IsCell(self); self = KosPair_CDR(self))
    length++;

  if (! KosNilP(self))
    length++;

  return PyLong_FromSsize_t(length);
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
  Py_ssize_t mu, lambda;

  /* a recursive spine has no improper end, so it counts as proper */
  if (pair_spine_cycle(self, &mu, &lambda))
    return 1;

  for (; KosPair_IsCell(self); self = KosPair_CDR(self))
    ;
  return KosNilP(self);
}


static PyObject *pair_is_proper(PyObject *self, PyObject *_noargs) {
  return PyBool_FromLong(KosPair_IsProper(self));
}


long KosPair_IsRecursive(PyObject *self) {
  Py_ssize_t mu, lambda;

  return pair_spine_cycle(self, &mu, &lambda);
}


static PyObject *pair_is_recursive(PyObject *self, PyObject *_noargs) {
  return PyBool_FromLong(KosPair_IsRecursive(self));
}


/**
   Walk the cons chain from self to the last pair. Returns the last
   pair, or NULL with ValueError set if the spine is recursive. On
   success, *out_cdr receives the cdr of the last pair (caller does
   not take a new reference).
 */
static KosPair *pair_find_last(PyObject *self, PyObject **out_cdr) {
  KosPair *last = NULL;
  Py_ssize_t mu, lambda;

  if (pair_spine_cycle(self, &mu, &lambda)) {
    PyErr_SetString(PyExc_ValueError,
                    "cannot append/extend/splice: recursive pair");
    return NULL;
  }

  for (; KosPair_IsCell(self); self = KosPair_CDR(self))
    last = (KosPair *) self;

  if (last) {
    *out_cdr = KosPair_CDR(last);
    return last;
  }

  return NULL;
}


/**
   Convert a Python iterable (tuple, list, iterator, etc.) to a proper
   cons chain. An empty iterable is nil. Returns new pair or nil, or
   NULL on error.
 */
static PyObject *pair_iterable_to_proper_cons(PyObject *obj) {
  PyObject *it = PyObject_GetIter(obj);
  PyObject *coll;
  PyObject *item;
  PyObject *result;

  if (! it)
    return NULL;

  coll = PyList_New(0);
  if (! coll) {
    Py_DECREF(it);
    return NULL;
  }

  while ((item = PyIter_Next(it)) != NULL) {
    if (PyList_Append(coll, item) < 0) {
      Py_DECREF(item);
      Py_DECREF(coll);
      Py_DECREF(it);
      return NULL;
    }
    Py_DECREF(item);
  }
  Py_DECREF(it);

  if (PyErr_Occurred()) {
    Py_DECREF(coll);
    return NULL;
  }

  if (PyList_GET_SIZE(coll) == 0) {
    Py_DECREF(coll);
    Py_INCREF(KosNil);
    return KosNil;
  }

  if (PyList_Append(coll, KosNil) < 0) {
    Py_DECREF(coll);
    return NULL;
  }

  result = KosPair_Cons(coll);
  Py_DECREF(coll);
  return result;
}


static PyObject *pair_append(PyObject *self, PyObject *obj) {
  PyObject *last_cdr;
  KosPair *last;
  PyObject *new_pair;

  if (KosNilP(self)) {
    PyErr_SetString(PyExc_TypeError, "cannot append to nil");
    return NULL;
  }

  last = pair_find_last(self, &last_cdr);

  if (! last)
    return NULL;

  if (KosNilP(last_cdr)) {
    new_pair = KosPair_New(obj, KosNil);
  } else {
    new_pair = KosPair_New(last_cdr, obj);
  }

  if (! new_pair)
    return NULL;

  KosPair_SETCDR(last, new_pair);
  Py_DECREF(new_pair);
  Py_RETURN_NONE;
}


static void pair_splice_at_last(KosPair *last, PyObject *last_cdr,
                                PyObject *splice_head) {
  PyObject *bridge;

  if (KosNilP(last_cdr)) {
    KosPair_SETCDR(last, splice_head);
  } else {
    bridge = KosPair_New(last_cdr, splice_head);
    if (bridge) {
      KosPair_SETCDR(last, bridge);
      Py_DECREF(bridge);
    }
  }
}


static PyObject *pair_extend(PyObject *self, PyObject *obj) {
  PyObject *last_cdr;
  KosPair *last;
  PyObject *splice_head = NULL;

  if (KosNilP(self)) {
    PyErr_SetString(PyExc_TypeError, "cannot extend nil");
    return NULL;
  }

  if (KosPair_IsCell(obj)) {
    splice_head = pair_copy(obj, NULL);
  } else {
    splice_head = pair_iterable_to_proper_cons(obj);
  }

  if (! splice_head)
    return NULL;

  /* the iterable has run; only now is the spine's last cell found */
  last = pair_find_last(self, &last_cdr);

  if (! last) {
    Py_DECREF(splice_head);
    return NULL;
  }

  pair_splice_at_last(last, last_cdr, splice_head);
  Py_DECREF(splice_head);
  Py_RETURN_NONE;
}


static PyObject *pair_splice(PyObject *self, PyObject *obj) {
  PyObject *last_cdr;
  KosPair *last;
  PyObject *splice_head = NULL;

  if (KosNilP(self)) {
    PyErr_SetString(PyExc_TypeError, "cannot splice nil");
    return NULL;
  }

  if (KosPair_IsCell(obj)) {
    Py_INCREF(obj);
    splice_head = obj;
  } else {
    splice_head = pair_iterable_to_proper_cons(obj);
  }

  if (! splice_head)
    return NULL;

  /* the iterable has run; only now is the spine's last cell found */
  last = pair_find_last(self, &last_cdr);

  if (! last) {
    Py_DECREF(splice_head);
    return NULL;
  }

  pair_splice_at_last(last, last_cdr, splice_head);
  Py_DECREF(splice_head);
  Py_RETURN_NONE;
}


/**
   Walk the cells reachable from pair through cdrs and cell cars and
   give each a position. With fill unset every cell takes pos (NULL
   clears). With fill set a cell with no position takes pos and a cell
   that already has one ends its branch: neither it, its car nor the
   rest of its spine is touched, so what the reader positioned stays
   as it was and what a macro built around it is filled in.

   The walk is depth-first in the recursion's order but keeps its own
   stack: at a cell whose car is a cell the rest of the spine is
   pushed and the car walked first. Depth is bounded by memory, not the
   C stack. seen holds the cells visited, by address, so a cycle ends.

   The walk owns a reference to the cell it is at and to every cell on
   its stack: releasing a cell's old position runs finalizers, and one
   that cuts the spine must not free a cell the walk still holds.

   :return: 0, or -1 with PyErr set
*/
static int pwalk_position(
    PyObject *pair,
    PyObject *seen,
    PyObject *pos,
    int fill) {

  PyObject **stack = NULL;
  Py_ssize_t depth = 0, cap = 0;
  int res = -1;

  Py_INCREF(pair);
  for (;;) {
    while (KosPair_IsCell(pair)) {
      KosPair *sp = (KosPair *) pair;
      PyObject *pair_id = PyLong_FromVoidPtr(pair);
      PyObject *next;
      int visited;

      if (! pair_id)
        goto done;
      visited = PySet_Contains(seen, pair_id);
      if (visited < 0 || (! visited && PySet_Add(seen, pair_id) < 0)) {
        Py_DECREF(pair_id);
        goto done;
      }
      Py_DECREF(pair_id);
      if (visited)
        break;

      if (fill) {
        if (KosPair_POSITION(sp))
          break;
        Py_INCREF(pos);
        KosPair_POSITION(sp) = pos;
      } else {
        Py_XINCREF(pos);
        Py_XSETREF(KosPair_POSITION(sp), pos);
      }

      if (KosPair_IsCell(KosPair_CAR(sp))) {
        if (depth == cap) {
          Py_ssize_t ncap = cap ? cap * 2 : 64;
          PyObject **grown = PyMem_Realloc(
              stack, (size_t) ncap * sizeof(PyObject *));
          if (! grown) {
            PyErr_NoMemory();
            goto done;
          }
          stack = grown;
          cap = ncap;
        }
        next = KosPair_CDR(sp);
        Py_INCREF(next);
        stack[depth++] = next;
        next = KosPair_CAR(sp);
      } else {
        next = KosPair_CDR(sp);
      }

      Py_INCREF(next);
      Py_SETREF(pair, next);
    }

    Py_DECREF(pair);
    if (depth == 0) {
      pair = NULL;
      break;
    }
    pair = stack[--depth];
  }
  res = 0;

 done:
  Py_XDECREF(pair);
  while (depth > 0)
    Py_DECREF(stack[--depth]);
  PyMem_Free(stack);
  return res;
}


static int pwalk_setpos(PyObject *pair, PyObject *seen, PyObject *pos) {
  return pwalk_position(pair, seen, pos, 0);
}


static int pwalk_fillpos(PyObject *pair, PyObject *seen, PyObject *pos) {
  return pwalk_position(pair, seen, pos, 1);
}


static PyObject *pair_read_position(PyObject *pair);
static int pair_write_position(PyObject *pair, PyObject *position);


static PyObject *pair_clear_position(PyObject *self,
                                     PyObject *args, PyObject *kwds) {

  static char *keywords[] = { "follow", NULL };
  int follow = 0;

  if (KosNilP(self))
    Py_RETURN_NONE;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "|p", keywords, &follow))
    return NULL;

  if (follow) {
    PyObject *seen = PySet_New(NULL);
    int r;
    if (! seen)
      return NULL;
    r = pwalk_setpos(self, seen, NULL);
    Py_DECREF(seen);
    if (r < 0)
      return NULL;

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

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|p", keywords,
                                    &position, &follow))
    return NULL;

  if (follow) {
    PyObject *seen = PySet_New(NULL);
    int r;
    if (! seen)
      return NULL;
    r = pwalk_setpos(self, seen, position);
    Py_DECREF(seen);
    if (r < 0)
      return NULL;

  } else {
    if (pair_write_position(self, position) < 0)
      return NULL;
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
    int r;
    if (! seen)
      return NULL;
    r = pwalk_fillpos(self, seen, position);
    Py_DECREF(seen);
    if (r < 0)
      return NULL;

  } else if (! KosNilP(self) && ! KosPair_POSITION(self)) {
    /* a nil cell takes no position, as for set and clear */
    Py_INCREF(position);
    KosPair_POSITION(self) = position;
  }

  Py_RETURN_NONE;
}


static PyObject *pair_get_position(PyObject *self, PyObject *_noargs) {
  return pair_read_position(self);
}


static PyMethodDef pair_methods[] = {
  { "__copy__", (PyCFunction) pair_copy, METH_NOARGS,
    "P.__copy__()" },

  { "copy", (PyCFunction) pair_copy, METH_NOARGS,
    "P.copy()" },

  { "__kos_repr__", (PyCFunction) pair_kos_repr, METH_NOARGS,
    "P.__kos_repr__() -> str\n"
    "Koskari-style list string for this pair spine." },

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

  { "append", (PyCFunction) pair_append, METH_O,
    "P.append(obj)" },

  { "extend", (PyCFunction) pair_extend, METH_O,
    "P.extend(obj)" },

  { "splice", (PyCFunction) pair_splice, METH_O,
    "P.splice(obj)" },

  { "clear_position", (PyCFunction) pair_clear_position,
    METH_VARARGS|METH_KEYWORDS,
    "P.clear_position(follow=False)" },

  { "set_position", (PyCFunction) pair_set_position,
    METH_VARARGS|METH_KEYWORDS,
    "P.set_position(position, follow=False)" },

  { "fill_position", (PyCFunction) pair_fill_position,
    METH_VARARGS|METH_KEYWORDS,
    "P.fill_position(position, follow=False)\n"
    "Give this cell position when it has none. With follow, do the same\n"
    "along the cdr spine and into cell cars; a cell that already has a\n"
    "position ends its branch, subtree included." },

  { "get_position", (PyCFunction) pair_get_position, METH_NOARGS,
    "P.get_position()" },

  { NULL, NULL, 0, NULL },
};


static PyObject *pair_read_car(PyObject *pair) {
  PyObject *result = NULL;

  if (! KosPair_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "car argument must be pair");

  } else if (KosNilP(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot get car of nil");

  } else {
    result = KosPair_CAR(pair);
    Py_INCREF(result);
  }

  return result;
}


static PyObject *pair_read_cdr(PyObject *pair) {
  PyObject *result = NULL;

  if (! KosPair_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "cdr argument must be pair");

  } else if (KosNilP(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot get cdr of nil");

  } else {
    result = KosPair_CDR(pair);
    Py_INCREF(result);
  }

  return result;
}


static PyObject *pair_read_position(PyObject *pair) {
  PyObject *position = NULL;

  if (! KosPair_Check(pair)) {
    PyErr_SetString(PyExc_TypeError, "position argument must be pair");
    return NULL;
  }

  position = KosPair_POSITION(pair);
  if (position) {
    Py_INCREF(position);
    return position;
  }

  Py_RETURN_NONE;
}


/**
   Store val as the car of pair. A NULL val is a deletion request, which
   a car cannot honor: every cell carries a head
*/
static int pair_write_car(PyObject *pair, PyObject *val) {

  if (KosNilP(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot set car of nil");
    return -1;
  }

  if (! val) {
    PyErr_SetString(PyExc_TypeError, "cannot delete car of pair");
    return -1;
  }

  KosPair_SETCAR(pair, val);
  return 0;
}


/**
   Store val as the cdr of pair. A NULL val is a deletion request, which
   truncates the list: the cdr becomes the empty list matching the cell's
   own style, bracket-nil for a bracket cell and nil otherwise
*/
static int pair_write_cdr(PyObject *pair, PyObject *val) {

  if (KosNilP(pair)) {
    PyErr_SetString(PyExc_TypeError, "cannot set cdr of nil");
    return -1;
  }

  if (! val)
    val = KosBracketPair_Check(pair) ? KosBracketNil : KosNil;

  KosPair_SETCDR(pair, val);
  return 0;
}


static int pair_write_position(PyObject *pair, PyObject *position) {

  if (KosNilP(pair)) {
    return 0;
  }

  Py_ASSIGN(KosPair_POSITION(pair), position);
  return 0;
}


static PyObject *pair_get_car(PyObject *self, void *closure) {
  (void) closure;
  return pair_read_car(self);
}


static int pair_set_car(PyObject *self, PyObject *value, void *closure) {
  (void) closure;
  return pair_write_car(self, value);
}


static PyObject *pair_get_cdr(PyObject *self, void *closure) {
  (void) closure;
  return pair_read_cdr(self);
}


static int pair_set_cdr(PyObject *self, PyObject *value, void *closure) {
  (void) closure;
  return pair_write_cdr(self, value);
}


static PyObject *pair_get_position_attr(PyObject *self, void *closure) {
  (void) closure;
  return pair_read_position(self);
}


static int pair_set_position_attr(
    PyObject *self,
    PyObject *value,
    void *closure) {

  (void) closure;

  if (! value) {
    if (KosNilP(self))
      return 0;

    Py_CLEAR(KosPair_POSITION(self));
    return 0;
  }

  return pair_write_position(self, value);
}


static PyGetSetDef pair_getsets[] = {
  {"car", pair_get_car, pair_set_car, NULL, NULL},
  {"cdr", pair_get_cdr, pair_set_cdr, NULL, NULL},
  {"position", pair_get_position_attr, pair_set_position_attr, NULL, NULL},
  {NULL},
};


static PySequenceMethods pair_as_sequence = {
  .sq_length = (lenfunc) pair_len,
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
  .tp_getset = pair_getsets,
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


/* Shared allocation for the pair family (pair and bracket-pair). They are
   layout-identical, so one free list serves both; popping a recycled cell
   stamps it with the requested type via Py_SET_TYPE, which is what keeps a
   pair from ever leaking out as a bracket-pair or vice versa. The free-list
   trace is intentionally unified under "pair". */
static PyObject *pair_alloc(PyTypeObject *type,
    PyObject *head, PyObject *tail) {

  KosPair *self = NULL;

  if (! (head && tail)) {
    PyErr_Format(PyExc_TypeError, "%s requires a head and a tail",
                 type->tp_name);
    return NULL;
  }

  if (pair_free_list) {
    self = pair_free_list;
    pair_free_list = (KosPair *) KosPair_CDR(self);
    pair_free_count--;
    KOS_TRACE_FL_ALLOC("pair", 1);
    Py_SET_TYPE(self, type);
    /* the revival CPython's own type freelists use: a refcount of one,
       and in a debug build the reference total and trace chain the
       dealloc gave up */
    _Py_NewReference((PyObject *) self);

  } else {
    KOS_TRACE_FL_ALLOC("pair", 0);
    self = PyObject_GC_New(KosPair, type);
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


PyObject *KosPair_New(PyObject *head, PyObject *tail) {
  return pair_alloc(&KosPairType, head, tail);
}


/* === BracketPairType === */


static PyObject *bracket_pair_new(PyTypeObject *type,
                                  PyObject *args, PyObject *kwds) {

  PyObject *head = NULL;
  PyObject *tail = NULL;

  (void) type;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "bracket-pair takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "OO:bracket-pair", &head, &tail))
    return NULL;

  return KosBracketPair_New(head, tail);
}


static PyObject *bracket_pair_repr(PyObject *self) {
  return pair_repr_inner(self, _str_bracket_cons_paren);
}


static PyObject *bracket_pair_str(PyObject *self) {
  return pair_render_inner(
      self, _str_open_bracket, _str_close_bracket, pair_elem_str);
}


static PyObject *bracket_pair_kos_repr(PyObject *self, PyObject *_unused) {
  (void) _unused;
  return pair_render_inner(
      self, _str_open_bracket, _str_close_bracket, KosUtils_Repr);
}


static PyMethodDef bracket_pair_methods[] = {
  { "__kos_repr__", (PyCFunction) bracket_pair_kos_repr, METH_NOARGS,
    "P.__kos_repr__() -> str\n"
    "Koskari-style bracket-list string for this pair spine." },

  { NULL, NULL, 0, NULL },
};


PyTypeObject KosBracketPairType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "bracket-pair",
  sizeof(KosPair),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,
  .tp_base = &KosPairType,
  .tp_methods = bracket_pair_methods,
  .tp_new = bracket_pair_new,
  .tp_dealloc = pair_dealloc,
  .tp_traverse = pair_traverse,
  .tp_clear = pair_clear,

  .tp_repr = bracket_pair_repr,
  .tp_str = bracket_pair_str,
};


PyObject *KosBracketPair_New(PyObject *head, PyObject *tail) {
  return pair_alloc(&KosBracketPairType, head, tail);
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


static void nil_dealloc(const PyObject *self) {
  (void) self;
  ;  /* nil is statically allocated, do not free it */
}


/* The nil types take the pair's GC flag, traverse and clear by
   inheritance, but their singletons are static objects with no GC
   header in front of them: the collector's visit callbacks would read
   the bytes before the singleton as one. tp_is_gc answering no is
   CPython's own way for a GC type whose static instances carry no
   header, the way static type objects do it. */
static int nil_is_gc(PyObject *self) {
  (void) self;
  return 0;
}


static PyObject *nil_repr(PyObject *self) {
  Py_INCREF(_str_nil);
  return _str_nil;
}


static PyObject *nil_str(PyObject *self) {
  Py_INCREF(_str_empty_paren);
  return _str_empty_paren;
}


static PyObject *nil_kos_repr(PyObject *self, PyObject *Py_UNUSED(_unused)) {
  Py_INCREF(_str_empty_paren);
  return _str_empty_paren;
}


static PyObject *nil_iter(PyObject *self) {
  KosPairIterator *i = PyObject_New(KosPairIterator, &KosPairIteratorType);
  if(! i)
    return NULL;

  i->index = 0;
  i->pair = NULL;

  return (PyObject *) i;
}


static Py_ssize_t nil_length(const PyObject *self) {
  (void) self;
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
  .sq_length = (lenfunc) nil_length,
  .sq_item = nil_getitem,
  .sq_ass_item = nil_setitem,
};


static int nil_bool(const PyObject *v) {
  (void) v;
  return 0;
}


static PyNumberMethods nil_as_number = {
  .nb_bool = (inquiry) nil_bool,
};


static PyMethodDef nil_methods[] = {
  { "__kos_repr__", (PyCFunction) nil_kos_repr, METH_NOARGS,
    "nil.__kos_repr__() -> str\n"
    "Koskari-style nil token." },

  { NULL, NULL, 0, NULL },
};


PyTypeObject KosNilType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "NilType",
  sizeof(KosPair),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_base = &KosPairType,
  .tp_new = nil_new,
  .tp_dealloc = (destructor) nil_dealloc,
  .tp_is_gc = nil_is_gc,

  .tp_traverse = NULL,
  .tp_clear = NULL,

  .tp_repr = nil_repr,
  .tp_str = nil_str,
  .tp_methods = nil_methods,
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


/* === BracketNilType === */


/* bracket-nil is the parallel empty-list singleton for the bracket
   style. It is a sibling of nil, not related by inheritance: it is-a
   bracket-pair (and thus a pair), but it is not a NilType. The two are
   unified only by KosNilP, the identity test against either singleton. */


static PyObject *bracket_nil_new(PyTypeObject *type,
                                 PyObject *args, PyObject *kwds) {

  (void) type;

  if (PyTuple_Size(args) || (kwds && PyDict_Size(kwds))) {
    PyErr_SetString(PyExc_TypeError, "BracketNilType takes no arguments");
    return NULL;
  }

  Py_INCREF(KosBracketNil);
  return KosBracketNil;
}


static void bracket_nil_dealloc(const PyObject *self) {
  (void) self;
  ;  /* bracket-nil is statically allocated, do not free it */
}


static PyObject *bracket_nil_repr(PyObject *self) {
  (void) self;
  Py_INCREF(_str_bracket_nil);
  return _str_bracket_nil;
}


static PyObject *bracket_nil_str(PyObject *self) {
  (void) self;
  Py_INCREF(_str_empty_bracket);
  return _str_empty_bracket;
}


static PyObject *bracket_nil_kos_repr(PyObject *self,
                                      PyObject *Py_UNUSED(_unused)) {
  Py_INCREF(_str_empty_bracket);
  return _str_empty_bracket;
}


static PyMethodDef bracket_nil_methods[] = {
  { "__kos_repr__", (PyCFunction) bracket_nil_kos_repr, METH_NOARGS,
    "bracket-nil.__kos_repr__() -> str\n"
    "Koskari-style empty bracket-list token." },

  { NULL, NULL, 0, NULL },
};


PyTypeObject KosBracketNilType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "BracketNilType",
  sizeof(KosPair),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_base = &KosBracketPairType,
  .tp_new = bracket_nil_new,
  .tp_dealloc = (destructor) bracket_nil_dealloc,
  .tp_is_gc = nil_is_gc,

  .tp_traverse = NULL,
  .tp_clear = NULL,

  .tp_repr = bracket_nil_repr,
  .tp_str = bracket_nil_str,
  .tp_methods = bracket_nil_methods,
  .tp_iter = nil_iter,
  .tp_as_number = &nil_as_number,
  .tp_as_sequence = &nil_as_sequence,
};


KosPair _KosBracketNil = {
  PyObject_HEAD_INIT(&KosBracketNilType)
  .head = NULL,
  .tail = NULL,
  .position = NULL,
};


/**
  Build a proper chain of type's cells over count borrowed items,
  ended by nil. No count is nil itself.
*/
static PyObject *pair_cons_from_ptr(
    PyTypeObject *type,
    PyObject *nil,
    PyObject *const *ptr,
    Py_ssize_t count) {

  if (count <= 0) {
    Py_INCREF(nil);
    return nil;
  }

  PyObject *result = pair_alloc(type, ptr[0], nil);
  if (! result)
    return NULL;

  if (count == 1)
    return result;

  PyObject *work = ptr[count - 1];
  Py_INCREF(work);

  for (Py_ssize_t i = count - 2; i >= 1; i--) {
    PyObject *tmp = pair_alloc(type, ptr[i], work);
    Py_DECREF(work);
    if (! tmp) {
      Py_DECREF(result);
      return NULL;
    }
    work = tmp;
  }

  KosPair_SETCDR(result, work);
  Py_DECREF(work);
  return result;
}


/**
  Build a chain of type's cells over the members of a sequence, ended
  by nil. msg is the TypeError text for a non-sequence.
*/
static PyObject *pair_cons_members(
    PyTypeObject *type,
    PyObject *nil,
    PyObject *members,
    const char *msg) {

  PyObject *seq = PySequence_Fast(members, msg);
  PyObject *result;

  if (! seq)
    return NULL;

  result = pair_cons_from_ptr(
      type, nil, PySequence_Fast_ITEMS(seq), PySequence_Fast_GET_SIZE(seq));
  Py_DECREF(seq);
  return result;
}


PyObject *KosPair_Cons(PyObject *members) {
  return pair_cons_members(
      &KosPairType, KosNil, members,
      "cons members object must be a sequence");
}


PyObject *KosBracketPair_Cons(PyObject *members) {
  return pair_cons_members(
      &KosBracketPairType, KosBracketNil, members,
      "bracket-cons members object must be a sequence");
}


PyObject *KosPair_ConsFromPtr(PyObject *const *ptr, Py_ssize_t count) {
  return pair_cons_from_ptr(&KosPairType, KosNil, ptr, count);
}


PyObject *KosBracketPair_ConsFromPtr(PyObject *const *ptr, Py_ssize_t count) {
  return pair_cons_from_ptr(
      &KosBracketPairType, KosBracketNil, ptr, count);
}


/**
   Advance the cycle-detection cursor two links, under the same stop
   conditions as the walk it shadows. Reaching an exit retires the
   cursor: the walk itself arrives there and ends on its own terms. A
   cursor that instead lands on the walk's own position means the
   spine loops somewhere other than head.

   :param cursor: the fast cursor, retired in place at an exit
   :param head: the walk's starting pair, one of its stop conditions
*/
static void pair_cycle_cursor_step(PyObject **cursor, PyObject *head) {
  int i;

  for (i = 0; i < 2; i++) {
    PyObject *next = KosPair_CDR(*cursor);

    if (KosNilP(next) || next == head || ! KosPair_Check(next)) {
      *cursor = NULL;
      return;
    }
    *cursor = next;
  }
}


int KosPair_MakeRecursive(PyObject *pair) {
  if (KosNilP(pair) || ! KosPair_Check(pair))
    return -1;

  PyObject *const cdr = KosPair_CDR(pair);
  if (KosNilP(cdr) || cdr == pair) {
    /* single pair: self-loop; or already recursive */
    if (cdr != pair)
      KosPair_SETCDR(pair, pair);
    return 0;
  }

  PyObject *current = pair;
  PyObject *cursor = pair;

  while (1) {
    PyObject *next = KosPair_CDR(current);
    if (KosNilP(next) || next == pair)
      break;
    if (! KosPair_Check(next))
      return -1;
    current = next;

    if (cursor)
      pair_cycle_cursor_step(&cursor, pair);
    if (cursor == current) {
      PyErr_SetString(PyExc_ValueError,
                      "make_recursive: spine loops below the head");
      return -1;
    }
  }

  KosPair_SETCDR(current, pair);
  return 0;
}


int KosPair_BreakRecursive(PyObject *pair) {
  if (KosNilP(pair) || ! KosPair_Check(pair))
    return -1;

  PyObject *current = pair;
  PyObject *cursor = pair;

  while (1) {
    PyObject *cdr = KosPair_CDR(current);
    if (cdr == pair) {
      KosPair_SETCDR(current, KosNil);
      return 0;
    }
    if (KosNilP(cdr) || ! KosPair_Check(cdr))
      return -1;
    current = cdr;

    if (cursor)
      pair_cycle_cursor_step(&cursor, pair);
    if (cursor == current) {
      PyErr_SetString(PyExc_ValueError,
                      "break_recursive: spine loops below the head");
      return -1;
    }
  }
}


/* === module functions === */


static PyObject *m_cons(PyObject *mod, PyObject *args, PyObject *kwds) {
  (void) kwds;
  return KosPair_Cons(args);
}


static PyObject *m_bracket_cons(PyObject *mod, PyObject *args, PyObject *kwds) {
  (void) mod;
  (void) kwds;
  return KosBracketPair_Cons(args);
}


static PyObject *m_car(PyObject *mod, PyObject *pair) {
  (void) mod;
  return pair_read_car(pair);
}


static PyObject *m_cdr(PyObject *mod, PyObject *pair) {
  (void) mod;
  return pair_read_cdr(pair);
}


static PyObject *m_nilp(PyObject *mod, PyObject *value) {
  (void) mod;
  return PyBool_FromLong(KosNilP(value));
}


static PyObject *m_make_recursive(PyObject *mod, PyObject *pair) {
  if (KosPair_MakeRecursive(pair) < 0) {
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_ValueError, "make_recursive failed");
    return NULL;
  }
  Py_RETURN_NONE;
}


static PyObject *m_break_recursive(PyObject *mod, PyObject *pair) {
  if (KosPair_BreakRecursive(pair) < 0) {
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_ValueError, "break_recursive failed");
    return NULL;
  }
  Py_RETURN_NONE;
}


static PyObject *m_setcar(PyObject *mod, PyObject *args) {
  PyObject *p = NULL;
  PyObject *val = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "O!O", &KosPairType, &p, &val))
    return NULL;

  if (pair_write_car(p, val) < 0)
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *m_setcdr(PyObject *mod, PyObject *args) {
  PyObject *p = NULL;
  PyObject *val = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "O!O", &KosPairType, &p, &val))
    return NULL;

  if (pair_write_cdr(p, val) < 0)
    return NULL;

  Py_RETURN_NONE;
}


static PyMethodDef pair_mod_methods[] = {

  { "cons", (PyCFunction) m_cons, METH_VARARGS,
    "cons(head, *tail) -> new pair\n"
    "Build a pair chain from the given sequence. No tail implies nil." },

  { "bracket_cons", (PyCFunction) m_bracket_cons, METH_VARARGS,
    "bracket_cons(head, *tail) -> new bracket-pair\n"
    "Like cons, but builds bracket-pair cells. No args implies "
    "bracket-nil." },

  { "make_recursive", m_make_recursive, METH_O,
    "make_recursive(pair) -> None\n"
    "Set the tail-most link's cdr to the head, making the chain circular." },

  { "break_recursive", m_break_recursive, METH_O,
    "break_recursive(pair) -> None\n"
    "Set the first link whose cdr is the head to nil, breaking the cycle." },

  { "car", m_car, METH_O,
    "car(P) -> object\n"
    "Returns the head element of a koskari pair instance P."},

  { "cdr", m_cdr, METH_O,
    "cdr(P) -> object\n"
    "Returns the tail element of a koskari pair instance P." },

  { "nilp", m_nilp, METH_O,
    "nilp(value) -> bool\n"
    "True if value is an empty-list singleton (nil or bracket-nil)." },

  { "setcar", m_setcar, METH_VARARGS,
    "setcar(P, obj) -> None\n"
    "Assigns the head element of a koskari pair instance P to obj." },

  { "setcdr", m_setcdr, METH_VARARGS,
    "setcdr(P, obj) -> None\n"
    "Assigns the tail element of a koskari pair instance P to obj." },

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
  STR_CONST(_str_recursive_true, "recursive=True");
  STR_CONST(_str_space, " ");
  STR_CONST(_str_open_bracket, "[");
  STR_CONST(_str_close_bracket, "]");
  STR_CONST(_str_bracket_cons_paren, "bracket-cons(");
  STR_CONST(_str_empty_paren, "()");
  STR_CONST(_str_empty_bracket, "[]");
  STR_CONST(_str_bracket_nil, "bracket-nil");

  /* initialize types */
  if (PyType_Ready(&KosPairType))
    return -1;

  if (PyType_Ready(&KosPairIteratorType))
    return -1;

  if (PyType_Ready(&KosPairFollowerType))
    return -1;

  if (PyType_Ready(&KosNilType))
    return -1;

  if (PyType_Ready(&KosBracketPairType))
    return -1;

  if (PyType_Ready(&KosBracketNilType))
    return -1;

  /* the two empty-list singletons are static and never freed: a
     reference to either moves no refcount */
  kos_immortalize(KosNil);
  kos_immortalize(KosBracketNil);

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosPairType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosNilType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosBracketPairType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosBracketNilType))
    return -1;

  /* add types and nil to module dict */
  if (PyDict_SetItemString(dict, "pair", (PyObject *) &KosPairType))
    return -1;

  if (PyDict_SetItemString(dict, "nil", KosNil))
    return -1;

  if (PyDict_SetItemString(dict, "bracket_pair",
                           (PyObject *) &KosBracketPairType))
    return -1;

  if (PyDict_SetItemString(dict, "bracket_nil", KosBracketNil))
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
  Py_CLEAR(_str_recursive_true);
  Py_CLEAR(_str_space);


  Py_CLEAR(_str_open_bracket);
  Py_CLEAR(_str_close_bracket);
  Py_CLEAR(_str_bracket_cons_paren);
  Py_CLEAR(_str_empty_paren);
  Py_CLEAR(_str_empty_bracket);
  Py_CLEAR(_str_bracket_nil);

  return 0;
}


// The end.
