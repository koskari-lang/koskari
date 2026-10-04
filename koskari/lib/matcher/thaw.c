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
  Included by matcher.c. Thaw: the frozen matcher tuple to a native
  Matcher.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === thaw: frozen tuple -> Matcher === */


/**
   Convert a Python int to a uint32 node/slot/extern/const index. -1
   maps to KOS_MATCH_NO_INDEX. Out-of-range values raise ValueError.
*/
static int thaw_parse_index(
    PyObject *o,
    uint32_t *out,
    const char *what) {

  if (! PyLong_Check(o)) {
    PyErr_Format(PyExc_TypeError, "%s must be an int, got %.200s",
                 what, Py_TYPE(o)->tp_name);
    return -1;
  }
  Py_ssize_t v = PyLong_AsSsize_t(o);
  if (v == -1 && PyErr_Occurred())
    return -1;
  if (v < -1 || v > (Py_ssize_t) 0xFFFFFFFEu) {
    PyErr_Format(PyExc_ValueError,
                 "%s index out of range: %zd", what, v);
    return -1;
  }
  if (v == -1)
    *out = KOS_MATCH_NO_INDEX;
  else
    *out = (uint32_t) v;
  return 0;
}


/**
   Parse a non-negative uint32 for REST.min_tail / REPEAT.min_count.
*/
static int thaw_parse_uint32_nonneg(
    PyObject *o,
    uint32_t *out,
    const char *what) {

  if (! PyLong_Check(o)) {
    PyErr_Format(PyExc_TypeError, "%s must be an int, got %.200s",
                 what, Py_TYPE(o)->tp_name);
    return -1;
  }
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (PyErr_Occurred())
    return -1;
  if (v > (unsigned long) 0xFFFFFFFEu) {
    PyErr_Format(PyExc_ValueError, "%s out of range", what);
    return -1;
  }
  *out = (uint32_t) v;
  return 0;
}


static int thaw_parse_test_kind(PyObject *o, uint16_t *out) {
  if (PyLong_Check(o)) {
    long v = PyLong_AsLong(o);
    if (v < 0 || v >= KOS_MATCH_TEST_KIND_COUNT) {
      PyErr_Format(PyExc_ValueError, "test_kind out of range: %ld", v);
      return -1;
    }
    *out = (uint16_t) v;
    return 0;
  }
  if (! PyUnicode_Check(o)) {
    PyErr_SetString(PyExc_TypeError,
                    "test_kind must be str or int");
    return -1;
  }
  if (PyUnicode_CompareWithASCIIString(o, "ANY") == 0)
    *out = KOS_MATCH_TEST_ANY;
  else if (PyUnicode_CompareWithASCIIString(o, "LITERAL") == 0)
    *out = KOS_MATCH_TEST_LITERAL;
  else if (PyUnicode_CompareWithASCIIString(o, "TYPE") == 0)
    *out = KOS_MATCH_TEST_TYPE;
  else if (PyUnicode_CompareWithASCIIString(o, "PREDICATE") == 0)
    *out = KOS_MATCH_TEST_PREDICATE;
  else {
    PyErr_Format(PyExc_ValueError,
                 "unknown test_kind: %R", o);
    return -1;
  }
  return 0;
}


static int thaw_node(KosMatcherBuilder *b, PyObject *t) {
  if (! PyTuple_Check(t) || PyTuple_GET_SIZE(t) < 1) {
    PyErr_SetString(PyExc_TypeError,
                    "frozen node must be a non-empty tuple");
    return -1;
  }

  PyObject *tag = PyTuple_GET_ITEM(t, 0);
  if (! PyUnicode_Check(tag)) {
    PyErr_SetString(PyExc_TypeError, "node kind tag must be str");
    return -1;
  }

  KosMatchNode n;
  memset(&n, 0, sizeof(n));
  n.as.capture.subnode = KOS_MATCH_NO_INDEX;
  Py_ssize_t arity = PyTuple_GET_SIZE(t);

  if (PyUnicode_CompareWithASCIIString(tag, "TEST") == 0) {
    if (arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "TEST node tuple must be 3-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_TEST;
    if (thaw_parse_test_kind(PyTuple_GET_ITEM(t, 1),
                             &n.as.test.test_kind))
      return -1;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 2),
                         &n.as.test.ref_index, "TEST.ref_index"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "CAPTURE") == 0) {
    if (arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "CAPTURE node tuple must be 3-tuple "
                   "(kind, slot, subnode), got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_CAPTURE;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.capture.slot, "CAPTURE.slot"))
      return -1;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 2),
                         &n.as.capture.subnode, "CAPTURE.subnode"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "FORM") == 0) {
    if (arity != 2) {
      PyErr_Format(PyExc_ValueError,
                   "FORM node tuple must be 2-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_FORM;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.form.subnode, "FORM.subnode"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "APP") == 0) {
    if (arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "APP node tuple must be 3-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_APP;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.app.fn_extern, "APP.fn_extern"))
      return -1;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 2),
                         &n.as.app.subnode, "APP.subnode"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "MAP") == 0) {
    Py_ssize_t cnt = arity - 1;
    uint32_t *entries = NULL;
    if (cnt > 0) {
      entries = PyMem_Malloc(cnt * sizeof(uint32_t));
      if (! entries) {
        PyErr_NoMemory();
        return -1;
      }
    }
    for (Py_ssize_t i = 0; i < cnt; i++) {
      if (thaw_parse_index(PyTuple_GET_ITEM(t, 1 + i), &entries[i],
                           "MAP entry")) {
        PyMem_Free(entries);
        return -1;
      }
    }
    int rc = KosMatcherBuilder_AppendMap(b, entries, cnt);
    PyMem_Free(entries);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "MAP_ENTRY_CONST") == 0
             || PyUnicode_CompareWithASCIIString(tag,
                                                 "MAP_ENTRY_EXTERN") == 0) {
    if (arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "MAP_ENTRY node tuple must be 3-tuple, got %zd", arity);
      return -1;
    }
    uint16_t kind =
        (PyUnicode_CompareWithASCIIString(tag, "MAP_ENTRY_CONST") == 0)
        ? (uint16_t) KOS_MATCH_MAPKEY_CONST
        : (uint16_t) KOS_MATCH_MAPKEY_EXTERN;
    uint32_t key_ref, sub;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1), &key_ref,
                         "MAP_ENTRY.key_ref"))
      return -1;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 2), &sub,
                         "MAP_ENTRY.subnode"))
      return -1;
    int rc = KosMatcherBuilder_AppendMapEntry(b, kind, key_ref, sub);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "MATCH") == 0) {
    Py_ssize_t cnt = arity - 1;
    uint32_t *children = NULL;
    if (cnt > 0) {
      children = PyMem_Malloc(cnt * sizeof(uint32_t));
      if (! children) {
        PyErr_NoMemory();
        return -1;
      }
    }
    for (Py_ssize_t i = 0; i < cnt; i++) {
      if (thaw_parse_index(PyTuple_GET_ITEM(t, 1 + i), &children[i],
                           "MATCH child")) {
        PyMem_Free(children);
        return -1;
      }
    }
    int rc = KosMatcherBuilder_AppendMatch(b, children, cnt);
    PyMem_Free(children);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "GROUP") == 0) {
    Py_ssize_t cnt = arity - 1;
    uint32_t *children = NULL;
    if (cnt > 0) {
      children = PyMem_Malloc(cnt * sizeof(uint32_t));
      if (! children) {
        PyErr_NoMemory();
        return -1;
      }
    }
    for (Py_ssize_t i = 0; i < cnt; i++) {
      if (thaw_parse_index(PyTuple_GET_ITEM(t, 1 + i), &children[i],
                           "GROUP child")) {
        PyMem_Free(children);
        return -1;
      }
    }
    int rc = KosMatcherBuilder_AppendGroup(b, children, cnt);
    PyMem_Free(children);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "ONEOF") == 0) {
    Py_ssize_t cnt = arity - 1;
    uint32_t *children = NULL;
    if (cnt > 0) {
      children = PyMem_Malloc(cnt * sizeof(uint32_t));
      if (! children) {
        PyErr_NoMemory();
        return -1;
      }
    }
    for (Py_ssize_t i = 0; i < cnt; i++) {
      if (thaw_parse_index(PyTuple_GET_ITEM(t, 1 + i), &children[i],
                           "ONEOF child")) {
        PyMem_Free(children);
        return -1;
      }
    }
    int rc = KosMatcherBuilder_AppendOneof(b, children, cnt);
    PyMem_Free(children);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "REPEAT") == 0) {
    if (arity != 2 && arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "REPEAT node tuple must be 2- or 3-tuple, got %zd",
                   arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_REPEAT;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.repeat.subnode, "REPEAT.subnode"))
      return -1;
    n.as.repeat.min_count = 1;
    if (arity == 3) {
      if (thaw_parse_uint32_nonneg(PyTuple_GET_ITEM(t, 2),
                                   &n.as.repeat.min_count,
                                   "REPEAT.min_count"))
        return -1;
    }

  } else if (PyUnicode_CompareWithASCIIString(tag, "OPTIONAL") == 0) {
    if (arity != 2) {
      PyErr_Format(PyExc_ValueError,
                   "OPTIONAL node tuple must be 2-tuple, got %zd",
                   arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_OPTIONAL;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.optional.subnode, "OPTIONAL.subnode"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "REST") == 0) {
    if (arity != 2) {
      PyErr_Format(PyExc_ValueError,
                   "REST node tuple must be (REST, min_tail), got %zd",
                   arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_REST;
    if (thaw_parse_uint32_nonneg(PyTuple_GET_ITEM(t, 1),
                                 &n.as.rest.min_tail,
                                 "REST.min_tail"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "NOT") == 0) {
    if (arity != 2) {
      PyErr_Format(PyExc_ValueError,
                   "NOT node tuple must be 2-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_NOT;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.not_.subnode, "NOT.subnode"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "SAME") == 0) {
    if (arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "SAME node tuple must be 3-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_SAME;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.same.slot, "SAME.slot"))
      return -1;
    if (thaw_parse_uint32_nonneg(PyTuple_GET_ITEM(t, 2),
                                 &n.as.same.depth,
                                 "SAME.depth"))
      return -1;

  } else if (PyUnicode_CompareWithASCIIString(tag, "CLASS") == 0) {
    if (arity < 2) {
      PyErr_Format(PyExc_ValueError,
                   "CLASS node tuple must have at least 2 entries, "
                   "got %zd", arity);
      return -1;
    }
    uint32_t type_extern;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1), &type_extern,
                         "CLASS.type_extern"))
      return -1;
    Py_ssize_t cnt = arity - 2;
    uint32_t *fields = NULL;
    if (cnt > 0) {
      fields = PyMem_Malloc(cnt * sizeof(uint32_t));
      if (! fields) {
        PyErr_NoMemory();
        return -1;
      }
    }
    for (Py_ssize_t i = 0; i < cnt; i++) {
      if (thaw_parse_index(PyTuple_GET_ITEM(t, 2 + i), &fields[i],
                           "CLASS field")) {
        PyMem_Free(fields);
        return -1;
      }
    }
    int rc = KosMatcherBuilder_AppendClass(b, type_extern, fields, cnt);
    PyMem_Free(fields);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "FIELD_POS") == 0) {
    if (arity != 2) {
      PyErr_Format(PyExc_ValueError,
                   "FIELD_POS node tuple must be 2-tuple, got %zd",
                   arity);
      return -1;
    }
    uint32_t sub;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1), &sub,
                         "FIELD_POS.subnode"))
      return -1;
    int rc = KosMatcherBuilder_AppendFieldPositional(b, sub);
    return (rc < 0) ? -1 : 0;

  } else if (PyUnicode_CompareWithASCIIString(tag, "FIELD_KW") == 0) {
    if (arity != 3) {
      PyErr_Format(PyExc_ValueError,
                   "FIELD_KW node tuple must be 3-tuple, got %zd",
                   arity);
      return -1;
    }
    uint32_t sub;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1), &sub,
                         "FIELD_KW.subnode"))
      return -1;
    PyObject *name = PyTuple_GET_ITEM(t, 2);
    if (! PyUnicode_Check(name)) {
      PyErr_Format(PyExc_TypeError,
                   "FIELD_KW attr name must be str (got %.200s)",
                   Py_TYPE(name)->tp_name);
      return -1;
    }
    int name_idx = -1;
    Py_ssize_t n_consts = PyList_GET_SIZE(b->consts);
    for (Py_ssize_t i = 0; i < n_consts; i++) {
      PyObject *cv = PyList_GET_ITEM(b->consts, i);
      if (PyUnicode_Check(cv)) {
        int eq = PyUnicode_Compare(cv, name);
        if (eq == 0) {
          name_idx = (int) i;
          break;
        }
        if (eq == -1 && PyErr_Occurred())
          return -1;
      }
    }
    if (name_idx < 0) {
      name_idx = KosMatcherBuilder_AddConst(b, name);
      if (name_idx < 0)
        return -1;
    }
    int rc = KosMatcherBuilder_AppendFieldKeyword(
        b, sub, (uint32_t) name_idx);
    return (rc < 0) ? -1 : 0;

  } else {
    PyErr_Format(PyExc_ValueError,
                 "unknown frozen node tag: %R", tag);
    return -1;
  }

  if (KosMatcherBuilder_AppendNode(b, &n) < 0)
    return -1;

  return 0;
}


static PyObject *thaw_matcher_tuple(PyObject *t) {
  if (! PyTuple_Check(t)) {
    PyErr_SetString(PyExc_TypeError, "frozen matcher must be a tuple");
    return NULL;
  }
  if (PyTuple_GET_SIZE(t) != 6) {
    PyErr_Format(PyExc_ValueError,
                 "frozen matcher must be 6-tuple, got %zd",
                 PyTuple_GET_SIZE(t));
    return NULL;
  }

  PyObject *tag = PyTuple_GET_ITEM(t, 0);
  if (! PyUnicode_Check(tag) ||
      PyUnicode_CompareWithASCIIString(tag, "MATCHER") != 0) {
    PyErr_SetString(PyExc_ValueError,
                    "frozen matcher must start with 'MATCHER' tag");
    return NULL;
  }

  PyObject *slots = PyTuple_GET_ITEM(t, 1);
  PyObject *externs = PyTuple_GET_ITEM(t, 2);
  PyObject *consts = PyTuple_GET_ITEM(t, 3);
  PyObject *nodes = PyTuple_GET_ITEM(t, 4);
  PyObject *entry_obj = PyTuple_GET_ITEM(t, 5);

  if (! PyTuple_Check(slots) || ! PyTuple_Check(externs) ||
      ! PyTuple_Check(consts) || ! PyTuple_Check(nodes)) {
    PyErr_SetString(PyExc_TypeError,
                    "slots/externs/consts/nodes must be tuples");
    return NULL;
  }
  if (! PyLong_Check(entry_obj)) {
    PyErr_SetString(PyExc_TypeError, "entry must be int");
    return NULL;
  }

  KosMatcherBuilder *b = KosMatcherBuilder_New();
  if (! b)
    return NULL;

  Py_ssize_t i, n;

  n = PyTuple_GET_SIZE(slots);
  for (i = 0; i < n; i++) {
    PyObject *name = PyTuple_GET_ITEM(slots, i);
    if (KosMatcherBuilder_AddSlot(b, name) < 0) {
      KosMatcherBuilder_Free(b);
      return NULL;
    }
  }

  n = PyTuple_GET_SIZE(externs);
  for (i = 0; i < n; i++) {
    PyObject *name = PyTuple_GET_ITEM(externs, i);
    if (KosMatcherBuilder_AddExtern(b, name) < 0) {
      KosMatcherBuilder_Free(b);
      return NULL;
    }
  }

  n = PyTuple_GET_SIZE(consts);
  for (i = 0; i < n; i++) {
    PyObject *value = PyTuple_GET_ITEM(consts, i);
    if (KosMatcherBuilder_AddConst(b, value) < 0) {
      KosMatcherBuilder_Free(b);
      return NULL;
    }
  }

  n = PyTuple_GET_SIZE(nodes);
  for (i = 0; i < n; i++) {
    PyObject *node_t = PyTuple_GET_ITEM(nodes, i);
    if (thaw_node(b, node_t)) {
      KosMatcherBuilder_Free(b);
      return NULL;
    }
  }

  Py_ssize_t entry_v = PyLong_AsSsize_t(entry_obj);
  if (entry_v == -1 && PyErr_Occurred()) {
    KosMatcherBuilder_Free(b);
    return NULL;
  }
  if (entry_v < 0 || entry_v > (Py_ssize_t) 0xFFFFFFFEu) {
    PyErr_Format(PyExc_ValueError,
                 "entry index out of range: %zd", entry_v);
    KosMatcherBuilder_Free(b);
    return NULL;
  }

  PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) entry_v);
  KosMatcherBuilder_Free(b);
  return m;
}


PyObject *KosMatcher_FromTuple(PyObject *tuple) {
  return thaw_matcher_tuple(tuple);
}


static PyObject *matcher_from_tuple(
    PyObject *cls,
    PyObject *args) {

  (void) cls;
  PyObject *t;
  if (! PyArg_ParseTuple(args, "O", &t))
    return NULL;
  return KosMatcher_FromTuple(t);
}


// The end.
