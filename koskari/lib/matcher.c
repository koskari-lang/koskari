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
   matcher — native low-level matcher core (Phase 1)

   Implements the tagged-node array, C-internal builder, and validator
   for the new matcher engine. The matching engine and subject adapters
   land in Phase 2; the frozen tagged-tuple thaw lands in Phase 3.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GNU General Public License v3
*/


#include "lib.h"


/* === builder internals === */


struct KosMatcherBuilder {
  KosMatchNode *nodes;
  Py_ssize_t node_count;
  Py_ssize_t node_capacity;

  PyObject *slots;     /* PyListObject during build */
  PyObject *externs;   /* PyListObject during build */
  PyObject *consts;    /* PyListObject during build */
};


static int builder_grow_nodes(KosMatcherBuilder *b, Py_ssize_t need) {
  if (b->node_count + need <= b->node_capacity)
    return 0;

  Py_ssize_t cap = b->node_capacity ? b->node_capacity : 8;
  while (cap < b->node_count + need)
    cap *= 2;

  KosMatchNode *nn = PyMem_Realloc(b->nodes, cap * sizeof(KosMatchNode));
  if (! nn) {
    PyErr_NoMemory();
    return -1;
  }

  b->nodes = nn;
  b->node_capacity = cap;
  return 0;
}


/* === width helpers === */


static uint32_t saturating_add(uint32_t a, uint32_t b) {
  if (a == KOS_MATCH_UNBOUNDED || b == KOS_MATCH_UNBOUNDED)
    return KOS_MATCH_UNBOUNDED;

  uint32_t r = a + b;
  if (r < a)
    return KOS_MATCH_UNBOUNDED;

  return r;
}


/**
   Sum the widths of a sibling chain: min widths add (saturating), max
   is UNBOUNDED as soon as any sibling is unbounded.
*/
static void chain_width(
    KosMatcher *m,
    uint32_t head,
    uint32_t *out_min,
    uint32_t *out_max) {

  uint32_t mn = 0, mx = 0;
  int has_unbounded = 0;
  for (uint32_t cidx = head;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = m->nodes[cidx].next) {
    mn = saturating_add(mn, m->nodes[cidx].min_width);
    if (m->nodes[cidx].max_width == KOS_MATCH_UNBOUNDED)
      has_unbounded = 1;
    else
      mx = saturating_add(mx, m->nodes[cidx].max_width);
  }
  *out_min = mn;
  *out_max = has_unbounded ? KOS_MATCH_UNBOUNDED : mx;
}


/* === per-append validation === */


static int append_check_index(
    KosMatcherBuilder *b,
    uint32_t idx,
    const char *what) {

  if (idx == KOS_MATCH_NO_INDEX)
    return 0;

  if ((Py_ssize_t) idx >= b->node_count) {
    PyErr_Format(PyExc_ValueError,
                 "matcher node forward reference: %s=%u (current count=%zd)",
                 what, (unsigned) idx, b->node_count);
    return -1;
  }

  return 0;
}


static int append_check_kind(uint16_t kind) {
  if (kind >= KOS_MATCH_NODE_KIND_COUNT) {
    PyErr_Format(PyExc_ValueError,
                 "invalid matcher node kind: %u", (unsigned) kind);
    return -1;
  }

  return 0;
}


static int append_check_test_kind(uint16_t kind) {
  if (kind >= KOS_MATCH_TEST_KIND_COUNT) {
    PyErr_Format(PyExc_ValueError,
                 "invalid matcher TEST kind: %u", (unsigned) kind);
    return -1;
  }

  return 0;
}


static int append_check_node(KosMatcherBuilder *b, const KosMatchNode *node) {
  if (append_check_kind(node->kind))
    return -1;

  switch (node->kind) {
  case KOS_MATCH_NODE_TEST:
    if (append_check_test_kind(node->as.test.test_kind))
      return -1;
    break;

  case KOS_MATCH_NODE_CAPTURE:
    if (append_check_index(b, node->as.capture.subnode, "capture.subnode"))
      return -1;
    break;

  case KOS_MATCH_NODE_FORM:
    if (append_check_index(b, node->as.form.subnode, "form.subnode"))
      return -1;
    break;

  case KOS_MATCH_NODE_MATCH:
  case KOS_MATCH_NODE_GROUP:
  case KOS_MATCH_NODE_ONEOF:
    PyErr_Format(PyExc_ValueError,
                 "AppendNode does not accept container kinds; "
                 "use AppendMatch, AppendGroup, or AppendOneof");
    return -1;

  case KOS_MATCH_NODE_CLASS:
  case KOS_MATCH_NODE_FIELD:
    PyErr_Format(PyExc_ValueError,
                 "AppendNode does not accept CLASS or FIELD kinds; "
                 "use AppendClass / AppendFieldPositional / "
                 "AppendFieldKeyword");
    return -1;

  case KOS_MATCH_NODE_MAP:
  case KOS_MATCH_NODE_MAP_ENTRY:
    PyErr_Format(PyExc_ValueError,
                 "AppendNode does not accept MAP or MAP_ENTRY kinds; "
                 "use AppendMap / AppendMapEntry");
    return -1;

  case KOS_MATCH_NODE_REPEAT:
    if (append_check_index(b, node->as.repeat.subnode, "repeat.subnode"))
      return -1;
    break;

  case KOS_MATCH_NODE_OPTIONAL:
    if (append_check_index(b, node->as.optional.subnode,
                           "optional.subnode"))
      return -1;
    break;

  case KOS_MATCH_NODE_REST:
    break;

  case KOS_MATCH_NODE_NOT:
    if (append_check_index(b, node->as.not_.subnode, "not.subnode"))
      return -1;
    break;

  case KOS_MATCH_NODE_SAME:
    break;

  case KOS_MATCH_NODE_APP:
    if (append_check_index(b, node->as.app.subnode, "app.subnode"))
      return -1;
    break;

  default:
    PyErr_Format(PyExc_ValueError, "unhandled node kind: %u",
                 (unsigned) node->kind);
    return -1;
  }

  return 0;
}


/* === public builder API === */


KosMatcherBuilder *KosMatcherBuilder_New(void) {
  KosMatcherBuilder *b = PyMem_Malloc(sizeof(KosMatcherBuilder));
  if (! b) {
    PyErr_NoMemory();
    return NULL;
  }

  b->nodes = NULL;
  b->node_count = 0;
  b->node_capacity = 0;
  b->slots = PyList_New(0);
  b->externs = PyList_New(0);
  b->consts = PyList_New(0);

  if (! b->slots || ! b->externs || ! b->consts) {
    KosMatcherBuilder_Free(b);
    return NULL;
  }

  return b;
}


void KosMatcherBuilder_Free(KosMatcherBuilder *b) {
  if (! b)
    return;

  PyMem_Free(b->nodes);
  Py_XDECREF(b->slots);
  Py_XDECREF(b->externs);
  Py_XDECREF(b->consts);
  PyMem_Free(b);
}


Py_ssize_t KosMatcherBuilder_NodeCount(KosMatcherBuilder *b) {
  return b ? b->node_count : 0;
}


static int builder_add_to_list(PyObject *list, PyObject *value) {
  Py_ssize_t idx = PyList_GET_SIZE(list);
  if (PyList_Append(list, value))
    return -1;

  if (idx > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher index space exhausted");
    return -1;
  }

  return (int) idx;
}


int KosMatcherBuilder_AddSlot(KosMatcherBuilder *b, PyObject *name) {
  if (! b || ! name) {
    PyErr_SetString(PyExc_ValueError, "AddSlot requires builder and name");
    return -1;
  }

  if (! PyUnicode_Check(name)) {
    PyErr_SetString(PyExc_TypeError, "slot name must be str");
    return -1;
  }

  return builder_add_to_list(b->slots, name);
}


int KosMatcherBuilder_AddExtern(KosMatcherBuilder *b, PyObject *name) {
  if (! b || ! name) {
    PyErr_SetString(PyExc_ValueError, "AddExtern requires builder and name");
    return -1;
  }

  if (! PyUnicode_Check(name)) {
    PyErr_SetString(PyExc_TypeError, "extern name must be str");
    return -1;
  }

  return builder_add_to_list(b->externs, name);
}


int KosMatcherBuilder_AddConst(KosMatcherBuilder *b, PyObject *value) {
  if (! b || ! value) {
    PyErr_SetString(PyExc_ValueError, "AddConst requires builder and value");
    return -1;
  }

  return builder_add_to_list(b->consts, value);
}


int KosMatcherBuilder_AppendNode(
    KosMatcherBuilder *b,
    const KosMatchNode *node) {

  if (! b || ! node) {
    PyErr_SetString(PyExc_ValueError, "AppendNode requires builder and node");
    return -1;
  }

  if (append_check_node(b, node))
    return -1;

  if (builder_grow_nodes(b, 1))
    return -1;

  Py_ssize_t off = b->node_count;
  b->nodes[off] = *node;
  /* Widths get filled by validator; next is filled when the node is
     stitched into a parent container (or remains NO_INDEX if isolated). */
  b->nodes[off].next = KOS_MATCH_NO_INDEX;
  b->nodes[off].min_width = 0;
  b->nodes[off].max_width = 0;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


static int builder_validate_chain_children(
    KosMatcherBuilder *b,
    const uint32_t *children,
    Py_ssize_t count,
    const char *what) {

  if (! b) {
    PyErr_Format(PyExc_ValueError,
                 "%s requires builder", what);
    return -1;
  }

  if (count < 0) {
    PyErr_Format(PyExc_ValueError,
                 "%s count must be non-negative", what);
    return -1;
  }

  if (count > 0 && ! children) {
    PyErr_Format(PyExc_ValueError,
                 "%s requires children pointer", what);
    return -1;
  }

  for (Py_ssize_t i = 0; i < count; i++) {
    uint32_t cidx = children[i];
    if (cidx == KOS_MATCH_NO_INDEX) {
      PyErr_Format(PyExc_ValueError,
                   "%s child index may not be NO_INDEX", what);
      return -1;
    }
    if ((Py_ssize_t) cidx >= b->node_count) {
      PyErr_Format(PyExc_ValueError,
                   "%s forward reference: child=%u (current count=%zd)",
                   what, (unsigned) cidx, b->node_count);
      return -1;
    }
    if (b->nodes[cidx].next != KOS_MATCH_NO_INDEX) {
      PyErr_Format(PyExc_ValueError,
                   "%s child %u already participates in another chain",
                   what, (unsigned) cidx);
      return -1;
    }
    for (Py_ssize_t j = 0; j < i; j++) {
      if (children[j] == cidx) {
        PyErr_Format(PyExc_ValueError,
                     "%s repeats child index %u", what, (unsigned) cidx);
        return -1;
      }
    }
  }

  for (Py_ssize_t i = 0; i + 1 < count; i++)
    b->nodes[children[i]].next = children[i + 1];

  return 0;
}


static int builder_append_chain_container(
    KosMatcherBuilder *b,
    KosMatchNodeKind kind,
    const uint32_t *children,
    Py_ssize_t count,
    const char *what) {

  if (builder_validate_chain_children(b, children, count, what))
    return -1;

  KosMatchNode n;
  memset(&n, 0, sizeof(n));
  n.kind = (uint16_t) kind;
  uint32_t head = (count > 0) ? children[0] : KOS_MATCH_NO_INDEX;
  switch (kind) {
  case KOS_MATCH_NODE_MATCH:
    n.as.match.first_child = head;
    break;
  case KOS_MATCH_NODE_GROUP:
    n.as.group.first_child = head;
    break;
  case KOS_MATCH_NODE_ONEOF:
    n.as.oneof.first_branch = head;
    break;
  default:
    PyErr_SetString(PyExc_ValueError,
                    "builder_append_chain_container: bad kind");
    return -1;
  }

  if (builder_grow_nodes(b, 1))
    return -1;

  Py_ssize_t off = b->node_count;
  b->nodes[off] = n;
  b->nodes[off].next = KOS_MATCH_NO_INDEX;
  b->nodes[off].min_width = 0;
  b->nodes[off].max_width = 0;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


int KosMatcherBuilder_AppendMatch(
    KosMatcherBuilder *b,
    const uint32_t *children,
    Py_ssize_t count) {

  return builder_append_chain_container(
      b, KOS_MATCH_NODE_MATCH, children, count, "AppendMatch");
}


int KosMatcherBuilder_AppendGroup(
    KosMatcherBuilder *b,
    const uint32_t *children,
    Py_ssize_t count) {

  return builder_append_chain_container(
      b, KOS_MATCH_NODE_GROUP, children, count, "AppendGroup");
}


int KosMatcherBuilder_AppendOneof(
    KosMatcherBuilder *b,
    const uint32_t *branches,
    Py_ssize_t count) {

  return builder_append_chain_container(
      b, KOS_MATCH_NODE_ONEOF, branches, count, "AppendOneof");
}


/**
   Internal: append a FIELD node. Caller has already vetted field_kind
   and (for keyword) name_const.
*/
static int builder_append_field(
    KosMatcherBuilder *b,
    uint16_t field_kind,
    uint32_t subnode,
    uint32_t name_const) {

  if (subnode != KOS_MATCH_NO_INDEX &&
      append_check_index(b, subnode, "field.subnode"))
    return -1;

  if (builder_grow_nodes(b, 1))
    return -1;

  Py_ssize_t off = b->node_count;
  KosMatchNode *n = &b->nodes[off];
  memset(n, 0, sizeof(*n));
  n->kind = (uint16_t) KOS_MATCH_NODE_FIELD;
  n->next = KOS_MATCH_NO_INDEX;
  n->min_width = 0;
  n->max_width = 0;
  n->as.field.field_kind = field_kind;
  n->as.field.reserved = 0;
  n->as.field.subnode = subnode;
  n->as.field.name_const = name_const;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


int KosMatcherBuilder_AppendFieldPositional(
    KosMatcherBuilder *b,
    uint32_t subnode) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendFieldPositional requires builder");
    return -1;
  }

  return builder_append_field(
      b, (uint16_t) KOS_MATCH_FIELD_POSITIONAL, subnode,
      KOS_MATCH_NO_INDEX);
}


int KosMatcherBuilder_AppendFieldKeyword(
    KosMatcherBuilder *b,
    uint32_t subnode,
    uint32_t name_const) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendFieldKeyword requires builder");
    return -1;
  }

  if (name_const == KOS_MATCH_NO_INDEX) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendFieldKeyword requires a name_const index");
    return -1;
  }

  Py_ssize_t n_consts = PyList_GET_SIZE(b->consts);
  if ((Py_ssize_t) name_const >= n_consts) {
    PyErr_Format(PyExc_ValueError,
                 "AppendFieldKeyword name_const %u out of range "
                 "(consts size=%zd)",
                 (unsigned) name_const, n_consts);
    return -1;
  }

  return builder_append_field(
      b, (uint16_t) KOS_MATCH_FIELD_KEYWORD, subnode, name_const);
}


int KosMatcherBuilder_AppendClass(
    KosMatcherBuilder *b,
    uint32_t type_extern,
    const uint32_t *fields,
    Py_ssize_t count) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError, "AppendClass requires builder");
    return -1;
  }

  if (count < 0) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendClass count must be non-negative");
    return -1;
  }

  if (count > 0 && ! fields) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendClass requires fields pointer");
    return -1;
  }

  for (Py_ssize_t i = 0; i < count; i++) {
    uint32_t fidx = fields[i];
    if (fidx == KOS_MATCH_NO_INDEX) {
      PyErr_SetString(PyExc_ValueError,
                      "AppendClass field index may not be NO_INDEX");
      return -1;
    }
    if ((Py_ssize_t) fidx >= b->node_count) {
      PyErr_Format(PyExc_ValueError,
                   "AppendClass forward reference: field=%u "
                   "(current count=%zd)",
                   (unsigned) fidx, b->node_count);
      return -1;
    }
    KosMatchNode *const fn = &b->nodes[fidx];
    if (fn->kind != KOS_MATCH_NODE_FIELD) {
      PyErr_Format(PyExc_ValueError,
                   "AppendClass child %u is not a FIELD node",
                   (unsigned) fidx);
      return -1;
    }
    if (fn->next != KOS_MATCH_NO_INDEX) {
      PyErr_Format(PyExc_ValueError,
                   "AppendClass child %u already participates in another "
                   "chain", (unsigned) fidx);
      return -1;
    }
    for (Py_ssize_t j = 0; j < i; j++) {
      if (fields[j] == fidx) {
        PyErr_Format(PyExc_ValueError,
                     "AppendClass repeats field index %u",
                     (unsigned) fidx);
        return -1;
      }
    }
  }

  uint32_t num_positional = 0;
  for (Py_ssize_t i = 0; i + 1 < count; i++)
    b->nodes[fields[i]].next = fields[i + 1];
  for (Py_ssize_t i = 0; i < count; i++) {
    if (b->nodes[fields[i]].as.field.field_kind
        == KOS_MATCH_FIELD_POSITIONAL)
      num_positional++;
  }

  if (builder_grow_nodes(b, 1))
    return -1;

  Py_ssize_t off = b->node_count;
  KosMatchNode *n = &b->nodes[off];
  memset(n, 0, sizeof(*n));
  n->kind = (uint16_t) KOS_MATCH_NODE_CLASS;
  n->next = KOS_MATCH_NO_INDEX;
  n->min_width = 0;
  n->max_width = 0;
  n->as.class_.type_extern = type_extern;
  n->as.class_.first_field =
      (count > 0) ? fields[0] : KOS_MATCH_NO_INDEX;
  n->as.class_.num_positional = num_positional;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


int KosMatcherBuilder_AppendMapEntry(
    KosMatcherBuilder *b,
    uint16_t key_kind,
    uint32_t key_ref,
    uint32_t subnode) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError, "AppendMapEntry requires builder");
    return -1;
  }

  if (key_kind >= (uint16_t) KOS_MATCH_MAPKEY_KIND_COUNT) {
    PyErr_Format(PyExc_ValueError,
                 "AppendMapEntry unknown key kind %u", (unsigned) key_kind);
    return -1;
  }

  if (key_ref == KOS_MATCH_NO_INDEX) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendMapEntry requires a key_ref index");
    return -1;
  }

  if (subnode != KOS_MATCH_NO_INDEX &&
      append_check_index(b, subnode, "map_entry.subnode"))
    return -1;

  if (builder_grow_nodes(b, 1))
    return -1;

  Py_ssize_t off = b->node_count;
  KosMatchNode *n = &b->nodes[off];
  memset(n, 0, sizeof(*n));
  n->kind = (uint16_t) KOS_MATCH_NODE_MAP_ENTRY;
  n->next = KOS_MATCH_NO_INDEX;
  n->min_width = 0;
  n->max_width = 0;
  n->as.map_entry.key_kind = key_kind;
  n->as.map_entry.reserved = 0;
  n->as.map_entry.key_ref = key_ref;
  n->as.map_entry.subnode = subnode;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


int KosMatcherBuilder_AppendMap(
    KosMatcherBuilder *b,
    const uint32_t *entries,
    Py_ssize_t count) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError, "AppendMap requires builder");
    return -1;
  }

  if (count < 0) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendMap count must be non-negative");
    return -1;
  }

  if (count > 0 && ! entries) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendMap requires entries pointer");
    return -1;
  }

  for (Py_ssize_t i = 0; i < count; i++) {
    uint32_t eidx = entries[i];
    if (eidx == KOS_MATCH_NO_INDEX) {
      PyErr_SetString(PyExc_ValueError,
                      "AppendMap entry index may not be NO_INDEX");
      return -1;
    }
    if ((Py_ssize_t) eidx >= b->node_count) {
      PyErr_Format(PyExc_ValueError,
                   "AppendMap forward reference: entry=%u "
                   "(current count=%zd)",
                   (unsigned) eidx, b->node_count);
      return -1;
    }
    KosMatchNode *const en = &b->nodes[eidx];
    if (en->kind != KOS_MATCH_NODE_MAP_ENTRY) {
      PyErr_Format(PyExc_ValueError,
                   "AppendMap child %u is not a MAP_ENTRY node",
                   (unsigned) eidx);
      return -1;
    }
    if (en->next != KOS_MATCH_NO_INDEX) {
      PyErr_Format(PyExc_ValueError,
                   "AppendMap child %u already participates in another "
                   "chain", (unsigned) eidx);
      return -1;
    }
    for (Py_ssize_t j = 0; j < i; j++) {
      if (entries[j] == eidx) {
        PyErr_Format(PyExc_ValueError,
                     "AppendMap repeats entry index %u",
                     (unsigned) eidx);
        return -1;
      }
    }
  }

  for (Py_ssize_t i = 0; i + 1 < count; i++)
    b->nodes[entries[i]].next = entries[i + 1];

  if (builder_grow_nodes(b, 1))
    return -1;

  Py_ssize_t off = b->node_count;
  KosMatchNode *n = &b->nodes[off];
  memset(n, 0, sizeof(*n));
  n->kind = (uint16_t) KOS_MATCH_NODE_MAP;
  n->next = KOS_MATCH_NO_INDEX;
  n->min_width = 0;
  n->max_width = 0;
  n->as.map.first_entry =
      (count > 0) ? entries[0] : KOS_MATCH_NO_INDEX;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


#include "matcher/validate.c"


/* === finalize === */


static PyObject *list_to_tuple(PyObject *list) {
  return PySequence_Tuple(list);
}


PyObject *KosMatcherBuilder_Finalize(
    KosMatcherBuilder *b,
    uint32_t entry) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError, "Finalize requires builder");
    return NULL;
  }

  if (b->node_count > 0 && (Py_ssize_t) entry >= b->node_count) {
    PyErr_Format(PyExc_ValueError,
                 "matcher entry index %u out of range (node_count=%zd)",
                 (unsigned) entry, b->node_count);
    return NULL;
  }

  PyObject *slots_t = list_to_tuple(b->slots);
  if (! slots_t)
    return NULL;

  PyObject *externs_t = list_to_tuple(b->externs);
  if (! externs_t) {
    Py_DECREF(slots_t);
    return NULL;
  }

  PyObject *consts_t = list_to_tuple(b->consts);
  if (! consts_t) {
    Py_DECREF(slots_t);
    Py_DECREF(externs_t);
    return NULL;
  }

  KosMatcher *m = PyObject_NewVar(KosMatcher, &KosMatcherType,
                                  b->node_count);
  if (! m) {
    Py_DECREF(slots_t);
    Py_DECREF(externs_t);
    Py_DECREF(consts_t);
    return NULL;
  }

  m->slots = slots_t;
  m->externs = externs_t;
  m->consts = consts_t;
  m->entry = entry;

  if (b->node_count > 0)
    memcpy(m->nodes, b->nodes, b->node_count * sizeof(KosMatchNode));

  /* Phase B: range checks for slot/extern/const indices */
  if (validate_slot_extern_const_indices(m)) {
    Py_DECREF(m);
    return NULL;
  }

  /* Phase B: bottom-up width compute */
  for (Py_ssize_t i = 0; i < Py_SIZE(m); i++) {
    if (compute_one_width(m, (uint32_t) i)) {
      Py_DECREF(m);
      return NULL;
    }
  }

  /* Phase B: REPEAT min-width and no-CAPTURE-in-NOT */
  if (validate_repeat_and_not(m)) {
    Py_DECREF(m);
    return NULL;
  }

  /* Phase B: CLASS / FIELD structural rules (depends on widths) */
  if (validate_class_and_field_structure(m)) {
    Py_DECREF(m);
    return NULL;
  }

  /* Phase C: per-branch validation */
  if (validate_branches(m)) {
    Py_DECREF(m);
    return NULL;
  }

  return (PyObject *) m;
}


#include "matcher/datum.c"


#include "matcher/subject.c"


/**
   Interned "__class__" and object's own __class__ descriptor, created
   by koskari_matcher_init and released by koskari_matcher_clear.
*/
static PyObject *kos_class_name = NULL;
static PyObject *kos_object_class_descr = NULL;


/**
   Whether isinstance's virtual-subclass fallback can change a nominal
   miss for inst. object_isinstance reads inst.__class__ and accepts
   when that is a subtype of cls, so the miss is final when the read
   provably yields Py_TYPE(inst): the type resolves attributes
   generically and __class__ on its MRO is object's own data
   descriptor, which outranks any instance dict entry.
*/
static int matcher_nominal_miss_is_final(PyObject *inst) {

  PyTypeObject *tp = Py_TYPE(inst);

  if (tp->tp_getattro != PyObject_GenericGetAttr)
    return 0;
  if (! kos_object_class_descr)
    return 0;
  return PyType_Lookup(tp, kos_class_name) == kos_object_class_descr;
}


/**
   isinstance for pattern tests, dispatch-free in both directions when
   cls is a type whose metaclass is exactly ``type``, which cannot
   carry a custom ``__instancecheck__``: a nominal identity/MRO hit is
   the answer isinstance would give, and a nominal miss is final when
   matcher_nominal_miss_is_final proves the virtual ``__class__`` read
   cannot change it. Any other metaclass, and a miss the proof does
   not cover, fall through to PyObject_IsInstance unchanged.
*/
static int matcher_isinstance(PyObject *inst, PyObject *cls) {
  if (likely(PyType_Check(cls))) {
    PyTypeObject *meta = Py_TYPE(cls);
    if (meta == &PyType_Type) {
      if (PyObject_TypeCheck(inst, (PyTypeObject *) cls))
        return 1;
      if (matcher_nominal_miss_is_final(inst))
        return 0;
    }
  }
  return PyObject_IsInstance(inst, cls);
}


/* === match engine === */


/* Extern env entries resolved into a stack buffer by the vectorcall
   entries before spilling to the heap. */
#define KOS_MATCH_ENV_INLINE 8


/* Rollback entries recorded inline before the change stack spills to
   the heap; a runtime arm rarely writes more than a few slots. */
#define KOS_MATCH_CHANGE_INLINE 8


/**
   One recorded slot write, undone by engine_rollback. A root write
   (dict NULL) moves the previous array entry into old_value; a GROUP
   frame write holds a ref to the frame and the previous value. A NULL
   old_value means the slot was unbound before the write.
*/
typedef struct {
  PyObject *dict;        /* GROUP frame, or NULL for the root array */
  uint32_t slot_idx;
  PyObject *old_value;
} DictChange;


/**
   Root captures live in a slot array aligned to the matcher's slot
   table, owned by the caller of the run core: an entry is a new ref
   once the slot fires, NULL until then. A GROUP pushes a dict frame
   over it; cur_captures is that frame, or NULL at the root.
*/
typedef struct {
  KosMatcher *m;
  KosMatchSubject *subj;
  PyObject *const *extern_env;   /* borrowed value array aligned to
                                    m->externs, or NULL */
  PyObject **root_slots;
  PyObject *cur_captures;        /* GROUP frame dict, or NULL at root */
  PyObject *capture_parent;      /* enclosing GROUP frame dict, or NULL
                                    when the enclosing frame is root */
  Py_ssize_t slot_count;
  DictChange *change_stack;
  Py_ssize_t change_count;
  Py_ssize_t change_capacity;
  DictChange change_inline[KOS_MATCH_CHANGE_INLINE];
  int in_from_datum;          /* matching inside unpacked substitution */
} MatchEngine;


/**
   Whether a capture at cell_idx is substitution: the walk is inside a
   substitution Datum, or the cell itself is one.
*/
static int cell_from_datum_flag(
    int in_from_datum,
    KosMatchSubject *subj,
    Py_ssize_t cell_idx) {

  if (in_from_datum)
    return 1;
  if (subj->kp_cars && cell_idx >= 0
      && cell_idx < subj->cached_length
      && KosDatum_Check(subj->kp_cars[cell_idx]))
    return 1;
  return 0;
}


static int capture_from_datum_flag(
    const MatchEngine *e,
    KosMatchSubject *subj,
    Py_ssize_t cell_idx) {

  return cell_from_datum_flag(e->in_from_datum, subj, cell_idx);
}


/**
   Whether captures from this subject are boxed in Datum. Only a walk
   over source cells boxes: a KosPair spine, whose cells carry
   positions, or a walk inside a substitution Datum, whose captures
   must carry from_datum. A tuple, list, or single-value subject
   yields raw values.
*/
static int subject_boxes_captures(
    const MatchEngine *e,
    const KosMatchSubject *subj) {

  return subj->kp_tails != NULL || e->in_from_datum;
}


static PyObject *kos_datum_from_spine_cell(
    MatchEngine *e,
    KosMatchSubject *subj,
    Py_ssize_t cell_idx,
    PyObject *datum_owned) {

  if (! subject_boxes_captures(e, subj))
    return datum_owned;

  PyObject *pos = Py_None;
  Py_INCREF(Py_None);
  if (subj->kp_tails && cell_idx >= 0
      && cell_idx < subj->cached_length) {
    PyObject *cell = subj->kp_tails[cell_idx];
    if (KosPair_Check(cell)) {
      PyObject *p = KosPair_POSITION(cell);
      Py_DECREF(Py_None);
      pos = p ? p : Py_None;
      Py_INCREF(pos);
    }
  }
  int from_datum = capture_from_datum_flag(e, subj, cell_idx);
  PyObject *out = kos_datum_new(
      datum_owned, pos, Py_None, from_datum);
  Py_DECREF(datum_owned);
  Py_DECREF(pos);
  return out;
}


static PyObject *wrap_slice_datums(
    MatchEngine *e,
    KosMatchSubject *subj,
    Py_ssize_t start,
    Py_ssize_t end) {

  Py_ssize_t n = end - start;
  if (n < 0)
    n = 0;
  PyObject *t = PyTuple_New(n);
  if (! t)
    return NULL;
  int boxes = subject_boxes_captures(e, subj);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *raw = subj->vt->get(subj, start + i);
    Py_INCREF(raw);
    PyObject *cell = raw;
    if (boxes) {
      cell = kos_datum_from_spine_cell(e, subj, start + i, raw);
      if (! cell) {
        Py_DECREF(t);
        return NULL;
      }
    }
    PyTuple_SET_ITEM(t, i, cell);
  }
  return t;
}


static PyObject *position_from_spine_cell(
    KosMatchSubject *subj,
    Py_ssize_t cell_idx) {

  PyObject *pos = Py_None;
  if (subj->kp_tails && cell_idx >= 0
      && cell_idx < subj->cached_length) {
    PyObject *cell = subj->kp_tails[cell_idx];
    if (KosPair_Check(cell)) {
      PyObject *p = KosPair_POSITION(cell);
      pos = p ? p : Py_None;
    }
  }
  Py_INCREF(pos);
  return pos;
}


static PyObject *source_from_spine_range(
    KosMatchSubject *subj,
    Py_ssize_t start,
    Py_ssize_t end) {

  Py_ssize_t span = end - start;
  if (span <= 0 || end == subj->cached_length)
    return subj->vt->tail(subj, start);

  if (span == 1) {
    PyObject *raw = subj->vt->get(subj, start);
    Py_INCREF(raw);
    return raw;
  }

  return subj->vt->slice(subj, start, end);
}


static int datum_frame_set_source(
    int in_from_datum,
    PyObject *frame,
    KosMatchSubject *subj,
    Py_ssize_t start,
    Py_ssize_t end) {

  if (! KosDatumFrame_Check(frame))
    return 0;

  PyObject *source = source_from_spine_range(subj, start, end);
  if (! source)
    return -1;

  PyObject *position = position_from_spine_cell(subj, start);
  if (! position) {
    Py_DECREF(source);
    return -1;
  }

  KosDatumFrame *f = (KosDatumFrame *) frame;
  PyObject *old_source = f->source;
  PyObject *old_position = f->position;
  f->source = source;
  f->position = position;
  f->from_datum = 0;
  for (Py_ssize_t i = start; i < end; i++) {
    if (cell_from_datum_flag(in_from_datum, subj, i)) {
      f->from_datum = 1;
      break;
    }
  }
  Py_DECREF(old_source);
  Py_DECREF(old_position);
  return 0;
}


/**
   A fresh capture frame for a walk over subj: a DatumFrame, which
   carries whole-cell provenance, on a pair spine; a plain dict
   otherwise.
*/
static PyObject *match_frame_new(const KosMatchSubject *subj) {
  if (subj->kp_tails)
    return kos_datum_frame_new();
  return PyDict_New();
}


/**
   Borrowed value of a slot in a frame: the root array when frame is
   NULL, else the frame dict by slot name. NULL when unbound, or with an
   exception set when the dict lookup fails (check PyErr_Occurred).
*/
static PyObject *engine_frame_get(
    const MatchEngine *e,
    PyObject *frame,
    uint32_t slot) {

  if (! frame)
    return e->root_slots[slot];
  PyObject *name = PyTuple_GET_ITEM(e->m->slots, slot);
  return PyDict_GetItemWithError(frame, name);
}


static PyObject *match_frame_copy(PyObject *frame) {

  if (! KosDatumFrame_Check(frame))
    return PyDict_Copy(frame);

  PyObject *copy = kos_datum_frame_new();
  if (! copy)
    return NULL;

  if (PyDict_Update(copy, frame)) {
    Py_DECREF(copy);
    return NULL;
  }

  KosDatumFrame *src = (KosDatumFrame *) frame;
  KosDatumFrame *dst = (KosDatumFrame *) copy;
  Py_INCREF(src->source);
  Py_INCREF(src->position);
  PyObject *old_source = dst->source;
  PyObject *old_position = dst->position;
  dst->source = src->source;
  dst->position = src->position;
  Py_DECREF(old_source);
  Py_DECREF(old_position);
  return copy;
}


static int engine_grow_changes(MatchEngine *e, Py_ssize_t need) {
  if (e->change_count + need <= e->change_capacity)
    return 0;
  Py_ssize_t cap = e->change_capacity * 2;
  while (cap < e->change_count + need)
    cap *= 2;
  DictChange *nn;
  if (e->change_stack == e->change_inline) {
    nn = PyMem_Malloc(cap * sizeof(DictChange));
    if (nn)
      memcpy(
          nn, e->change_inline, e->change_count * sizeof(DictChange));
  } else {
    nn = PyMem_Realloc(e->change_stack, cap * sizeof(DictChange));
  }
  if (! nn) {
    PyErr_NoMemory();
    return -1;
  }
  e->change_stack = nn;
  e->change_capacity = cap;
  return 0;
}


static int engine_set_slot(
    MatchEngine *e,
    uint32_t slot,
    PyObject *value) {

  if ((Py_ssize_t) slot >= e->slot_count) {
    PyErr_Format(PyExc_RuntimeError,
                 "internal: slot %u out of range", (unsigned) slot);
    Py_DECREF(value);
    return -1;
  }

  PyObject *name = PyTuple_GET_ITEM(e->m->slots, slot);

  if (engine_grow_changes(e, 1)) {
    Py_DECREF(value);
    return -1;
  }

  if (! e->cur_captures) {
    /* Root: the previous entry moves into the change record and the
       new value moves into the array. */
    DictChange *ch = &e->change_stack[e->change_count];
    ch->dict = NULL;
    ch->slot_idx = slot;
    ch->old_value = e->root_slots[slot];
    e->change_count++;
    e->root_slots[slot] = value;
    kos_datum_annotate_binding(value, name);
    return 0;
  }

  PyObject *d = e->cur_captures;
  PyObject *prev = PyDict_GetItemWithError(d, name);
  if (! prev && PyErr_Occurred()) {
    Py_DECREF(value);
    return -1;
  }

  DictChange *ch = &e->change_stack[e->change_count];
  ch->dict = d;
  Py_INCREF(d);
  ch->slot_idx = slot;
  Py_XINCREF(prev);
  ch->old_value = prev;
  e->change_count++;

  if (PyDict_SetItem(d, name, value)) {
    Py_DECREF(value);
    return -1;
  }
  kos_datum_annotate_binding(value, name);
  Py_DECREF(value);
  return 0;
}


static Py_ssize_t engine_snapshot(const MatchEngine *e) {
  return e->change_count;
}


static void engine_rollback(MatchEngine *e, Py_ssize_t snapshot) {
  while (e->change_count > snapshot) {
    e->change_count--;
    DictChange *c = &e->change_stack[e->change_count];
    if (! c->dict) {
      PyObject *cur = e->root_slots[c->slot_idx];
      e->root_slots[c->slot_idx] = c->old_value;
      Py_XDECREF(cur);
      continue;
    }
    PyObject *name = PyTuple_GET_ITEM(e->m->slots, c->slot_idx);
    if (! c->old_value) {
      if (PyDict_DelItem(c->dict, name)) {
        if (PyErr_ExceptionMatches(PyExc_KeyError))
          PyErr_Clear();
      }
    } else {
      if (PyDict_SetItem(c->dict, name, c->old_value))
        PyErr_Clear();
    }
    Py_XDECREF(c->old_value);
    Py_DECREF(c->dict);
  }
}


/**
   Drop rollback bookkeeping refs without mutating capture dicts.

   Each engine_set_slot records Py_INCREF(dict) and possibly Py_INCREF(old
   value) so engine_rollback can undo. When the overall match finishes
   without unwinding to snapshot zero, those extra refs must be released
   or the capture dicts (and their entries) leak.
*/
static void engine_discard_pending_change_refs(MatchEngine *e) {
  while (e->change_count > 0) {
    e->change_count--;
    DictChange *c = &e->change_stack[e->change_count];
    Py_XDECREF(c->old_value);
    c->old_value = NULL;
    Py_XDECREF(c->dict);
  }
}


static int engine_test(MatchEngine *e, KosMatchTest *t, PyObject *elem) {
  switch (t->test_kind) {
  case KOS_MATCH_TEST_ANY:
    return 1;

  case KOS_MATCH_TEST_LITERAL: {
    PyObject *lit = PyTuple_GET_ITEM(e->m->consts, t->ref_index);
    PyObject *got = matcher_peel_datum(elem);
    if (lit == got)
      return 1;
    int cmp = PyObject_RichCompareBool(lit, got, Py_EQ);
    if (cmp < 0)
      return -1;
    return cmp;
  }

  case KOS_MATCH_TEST_TYPE: {
    if (! e->extern_env) {
      PyErr_Format(PyExc_RuntimeError,
                   "TEST_TYPE requires an extern environment");
      return -1;
    }
    PyObject *cls = e->extern_env[t->ref_index];
    PyObject *got = matcher_peel_datum(elem);
    int r = matcher_isinstance(got, cls);
    if (r < 0)
      return -1;
    return r;
  }

  case KOS_MATCH_TEST_PREDICATE: {
    if (! e->extern_env) {
      PyErr_Format(PyExc_RuntimeError,
                   "TEST_PREDICATE requires an extern environment");
      return -1;
    }
    PyObject *pred = e->extern_env[t->ref_index];
    PyObject *got = matcher_peel_datum(elem);
    PyObject *res = matcher_call_extern(pred, got);
    if (! res)
      return -1;
    int r = PyObject_IsTrue(res);
    Py_DECREF(res);
    if (r < 0)
      return -1;
    return r;
  }

  default:
    PyErr_Format(PyExc_RuntimeError,
                 "unknown TEST kind %u",
                 (unsigned) t->test_kind);
    return -1;
  }
}


/**
   Build a Python dict containing all slot writes captured between
   `from_change` and the current change_count, expressed as
   {slot_name: value}. Used to materialize per-iteration sub-frames
   for named REPEAT captures.
*/
static PyObject *engine_changes_to_dict(
    MatchEngine *e,
    Py_ssize_t from_change) {

  PyObject *d = PyDict_New();
  if (! d)
    return NULL;
  for (Py_ssize_t i = from_change; i < e->change_count; i++) {
    uint32_t slot = e->change_stack[i].slot_idx;
    PyObject *name = PyTuple_GET_ITEM(e->m->slots, slot);
    PyObject *val = engine_frame_get(e, e->cur_captures, slot);
    if (! val) {
      if (PyErr_Occurred()) {
        Py_DECREF(d);
        return NULL;
      }
      continue;
    }
    if (PyDict_SetItem(d, name, val)) {
      Py_DECREF(d);
      return NULL;
    }
  }
  return d;
}


static int match_node(
    MatchEngine *e,
    uint32_t node_idx,
    Py_ssize_t *cursor,
    uint32_t trailing_min);


static int match_repeat(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min);


static int match_group(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min,
    PyObject **out_sub);


/**
   Match a width-1 sub-matcher against one projected value — a CLASS
   field, an APP call result, or a MAP entry lookup. The value is
   matched in its own synthesized single-element subject rather than
   being treated as a sequence itself. subnode may be
   KOS_MATCH_NO_INDEX for a presence-only projection (immediate match).
   val is borrowed; the caller retains ownership. Returns 1 on match,
   0 on no-match, -1 on error.
*/
static int match_projected_value(
    MatchEngine *e,
    uint32_t subnode,
    PyObject *val) {

  if (subnode == KOS_MATCH_NO_INDEX)
    return 1;

  KosMatchSubject sub;
  KosMatchSubject_FromSingle(val, &sub);

  KosMatchSubject *saved = e->subj;
  e->subj = &sub;

  Py_ssize_t sub_cursor = 0;
  int r = match_node(e, subnode, &sub_cursor, 0);

  e->subj = saved;
  KosMatchSubject_Release(&sub);

  if (r != 1)
    return r;
  if (sub_cursor != 1)
    return 0;
  return 1;
}


#include "matcher/match_form.c"
#include "matcher/match_group.c"
#include "matcher/match_rest.c"
#include "matcher/match_repeat.c"
#include "matcher/match_capture.c"
#include "matcher/match_match.c"
#include "matcher/match_oneof.c"
#include "matcher/match_optional.c"
#include "matcher/match_not.c"
#include "matcher/match_class.c"
#include "matcher/match_map.c"
#include "matcher/match_app.c"


static int match_node(
    MatchEngine *e,
    uint32_t node_idx,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  KosMatchNode *n = &e->m->nodes[node_idx];

  switch (n->kind) {
  case KOS_MATCH_NODE_TEST: {
    Py_ssize_t length = e->subj->vt->length(e->subj);
    /* Reserve trailing_min spine for siblings after this TEST. */
    if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
      return 0;
    PyObject *elem = e->subj->vt->get(e->subj, *cursor);
    int r = engine_test(e, &n->as.test, elem);
    if (r < 0)
      return -1;
    if (! r)
      return 0;
    (*cursor)++;
    return 1;
  }

  case KOS_MATCH_NODE_SAME: {
    Py_ssize_t length = e->subj->vt->length(e->subj);
    if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
      return 0;
    if (n->as.same.depth > 1)
      return 0;

    /* depth 0 reads the current frame; depth 1 the enclosing one,
       which exists only inside a GROUP and is the root array when
       that GROUP is the outermost. */
    PyObject *frame = e->cur_captures;
    if (n->as.same.depth == 1) {
      if (! e->cur_captures)
        return 0;
      frame = e->capture_parent;
    }

    if ((Py_ssize_t) n->as.same.slot >= e->slot_count)
      return 0;

    PyObject *prev = engine_frame_get(e, frame, n->as.same.slot);
    if (! prev) {
      if (PyErr_Occurred())
        return -1;
      return 0;
    }

    PyObject *elem = e->subj->vt->get(e->subj, *cursor);
    PyObject *got = matcher_peel_datum(elem);
    prev = matcher_peel_datum(prev);
    if (KosDatumFrame_Check(got) || KosDatumFrame_Check(prev))
      return 0;
    if (got == prev) {
      (*cursor)++;
      return 1;
    }
    int cmp = PyObject_RichCompareBool(got, prev, Py_EQ);
    if (cmp < 0)
      return -1;
    if (! cmp)
      return 0;
    (*cursor)++;
    return 1;
  }

  case KOS_MATCH_NODE_CAPTURE:
    return match_capture(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_FORM:
    return match_form(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_MATCH:
    return match_match(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_GROUP: {
    PyObject *sub = NULL;
    int r = match_group(e, n, cursor, trailing_min, &sub);
    if (r != 1)
      return r;
    Py_DECREF(sub);
    return 1;
  }

  case KOS_MATCH_NODE_ONEOF:
    return match_oneof(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_REPEAT:
    return match_repeat(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_OPTIONAL:
    return match_optional(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_REST:
    return match_rest(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_NOT:
    return match_not(e, n, cursor);

  case KOS_MATCH_NODE_CLASS:
    return match_class(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_APP:
    return match_app(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_MAP:
    return match_map(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_FIELD:
    PyErr_Format(PyExc_RuntimeError,
                 "FIELD node may not be matched directly (must be "
                 "reached via a CLASS chain)");
    return -1;

  case KOS_MATCH_NODE_MAP_ENTRY:
    PyErr_Format(PyExc_RuntimeError,
                 "MAP_ENTRY node may not be matched directly (must be "
                 "reached via a MAP chain)");
    return -1;

  default:
    PyErr_Format(PyExc_RuntimeError,
                 "unknown matcher node kind: %u", (unsigned) n->kind);
    return -1;
  }
}


static void matcher_clear_slots(KosMatcher *m, PyObject **slots) {

  Py_ssize_t n_slots = PyTuple_GET_SIZE(m->slots);

  for (Py_ssize_t i = 0; i < n_slots; i++)
    Py_CLEAR(slots[i]);
}


/**
   Run the matcher over the subject with an already-aligned extern env.
   ``slots`` is the caller's array of ``len(m->slots)`` entries,
   arriving all NULL; on a match (return 1) each slot that fired holds
   a new ref and the rest stay NULL. On no match (0) or error (-1,
   exception set) the array is all NULL again and nothing is owned.
   A match consumed the whole spine, so the cursor is the subject
   length.
*/
static int matcher_run_core(
    KosMatcher *m,
    KosMatchSubject *subj,
    PyObject *const *extern_env,
    PyObject **slots) {

  MatchEngine engine;
  MatchEngine *e = &engine;

  memset(e, 0, offsetof(MatchEngine, change_inline));
  e->m = m;
  e->subj = subj;
  e->extern_env = extern_env;
  e->root_slots = slots;
  e->slot_count = PyTuple_GET_SIZE(m->slots);
  e->change_stack = e->change_inline;
  e->change_capacity = KOS_MATCH_CHANGE_INLINE;
  e->in_from_datum = 0;

  Py_ssize_t cursor = 0;
  int r = 1;
  if (Py_SIZE(m) > 0)
    r = match_node(e, m->entry, &cursor, 0);

  int matched = 0;
  if (r == 1 && cursor == subj->vt->length(subj))
    matched = 1;

  engine_discard_pending_change_refs(e);
  if (e->change_stack != e->change_inline)
    PyMem_Free(e->change_stack);
  e->change_stack = e->change_inline;
  e->change_capacity = KOS_MATCH_CHANGE_INLINE;

  if (r < 0 || ! matched) {
    matcher_clear_slots(m, slots);
    return r < 0 ? -1 : 0;
  }
  return 1;
}


int KosMatcher_Run(
    KosMatcher *m,
    KosMatchSubject *subj,
    PyObject *const *extern_env,
    Py_ssize_t n_extern_env,
    PyObject **out_caps) {

  if (! m || ! out_caps) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_Run requires matcher and out");
    return -1;
  }
  *out_caps = NULL;

  Py_ssize_t n_slots = PyTuple_GET_SIZE(m->slots);

  PyObject *inline_slots[KOS_MATCH_SLOTS_INLINE];
  PyObject **slots = inline_slots;
  if (n_slots > KOS_MATCH_SLOTS_INLINE) {
    slots = PyMem_Malloc((size_t) n_slots * sizeof(PyObject *));
    if (! slots) {
      PyErr_NoMemory();
      return -1;
    }
  }

  int matched = KosMatcher_RunSlots(
      m, subj, extern_env, n_extern_env, slots);

  PyObject *root = NULL;
  if (matched == 1) {
    /* A capture-less match still materializes the frame: the caller
       contract (and the DatumFrame source/position it may carry) is
       the same whether or not any slot fired. The whole spine was
       consumed, and the walk is back outside any substitution. */
    root = match_frame_new(subj);
    if (! root) {
      matched = -1;
    } else {
      for (Py_ssize_t i = 0; i < n_slots; i++) {
        if (! slots[i])
          continue;
        PyObject *name = PyTuple_GET_ITEM(m->slots, i);
        if (PyDict_SetItem(root, name, slots[i])) {
          matched = -1;
          break;
        }
      }
      if (matched == 1
          && datum_frame_set_source(
              0, root, subj, 0, subj->vt->length(subj)))
        matched = -1;
      if (matched < 0)
        Py_CLEAR(root);
    }
  }

  for (Py_ssize_t i = 0; i < n_slots; i++)
    Py_XDECREF(slots[i]);
  if (slots != inline_slots)
    PyMem_Free(slots);

  *out_caps = root;
  return matched;
}


int KosMatcher_RunSlots(
    KosMatcher *m,
    KosMatchSubject *subj,
    PyObject *const *extern_env,
    Py_ssize_t n_extern_env,
    PyObject **out_slots) {

  if (! m || ! out_slots) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_RunSlots requires matcher and out");
    return -1;
  }

  /* The array is all NULL from here on every return path. */
  Py_ssize_t n_slots = PyTuple_GET_SIZE(m->slots);
  for (Py_ssize_t i = 0; i < n_slots; i++)
    out_slots[i] = NULL;

  if (! subj) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_RunSlots requires a subject");
    return -1;
  }

  Py_ssize_t n_externs = PyTuple_GET_SIZE(m->externs);
  if (n_extern_env != n_externs) {
    PyErr_Format(PyExc_TypeError,
                 "extern_env has %zd entries, expected %zd",
                 n_extern_env, n_externs);
    return -1;
  }
  if (n_externs > 0 && ! extern_env) {
    PyErr_Format(PyExc_TypeError,
                 "matcher requires %zd extern bindings", n_externs);
    return -1;
  }

  return matcher_run_core(m, subj, extern_env, out_slots);
}


PyObject *KosMatcher_Match(
    KosMatcher *m,
    KosMatchSubject *subj,
    PyObject *extern_env) {

  if (! m || ! subj) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_Match requires matcher and subject");
    return NULL;
  }

  Py_ssize_t n_externs = PyTuple_GET_SIZE(m->externs);
  PyObject *const *env = NULL;
  if (n_externs > 0) {
    if (! extern_env) {
      PyErr_Format(PyExc_TypeError,
                   "matcher requires %zd extern bindings", n_externs);
      return NULL;
    }
    if (! PyTuple_Check(extern_env)) {
      PyErr_SetString(PyExc_TypeError, "extern_env must be a tuple");
      return NULL;
    }
    if (PyTuple_GET_SIZE(extern_env) != n_externs) {
      PyErr_Format(PyExc_TypeError,
                   "extern_env has %zd entries, expected %zd",
                   PyTuple_GET_SIZE(extern_env), n_externs);
      return NULL;
    }
    env = &PyTuple_GET_ITEM(extern_env, 0);
  }

  PyObject *caps = NULL;
  int matched = KosMatcher_Run(m, subj, env, n_externs, &caps);
  if (matched < 0)
    return NULL;

  if (! matched) {
    caps = PyDict_New();
    if (! caps)
      return NULL;
  }

  PyObject *result = make_match_result(matched, caps);
  Py_DECREF(caps);
  return result;
}


/**
   Resolve one extern name from the inbuilt extern defaults. Borrowed
   reference, or NULL with the missing-binding TypeError (or a real
   error) set. The single home of that error.
*/
static PyObject *matcher_extern_default(PyObject *name) {
  PyObject *defaults = KosMatcher_ExternDefaults();
  if (! defaults)
    return NULL;
  PyObject *val = PyDict_GetItemWithError(defaults, name);
  if (! val && ! PyErr_Occurred())
    PyErr_Format(PyExc_TypeError,
                 "matcher missing extern binding: %R", name);
  return val;
}


/**
   Build the extern env tuple aligned to m->externs: each name resolves
   from the provided dict first (may be NULL), then the inbuilt extern
   defaults. Unknown provided names are ignored, matching the kwargs
   behavior of ``Matcher.match``. On success *out_env is a new tuple
   reference, or NULL when the matcher declares no externs (provided
   entries are then rejected). Returns 0, or -1 with an exception set.
*/
static int matcher_extern_env(
    KosMatcher *m,
    PyObject *provided,
    PyObject **out_env) {

  *out_env = NULL;
  Py_ssize_t n = PyTuple_GET_SIZE(m->externs);
  if (n == 0) {
    if (provided && PyDict_GET_SIZE(provided) > 0) {
      PyErr_SetString(PyExc_TypeError,
                      "matcher takes no extern bindings");
      return -1;
    }
    return 0;
  }

  PyObject *env = PyTuple_New(n);
  if (! env)
    return -1;

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *name = PyTuple_GET_ITEM(m->externs, i);
    PyObject *val = NULL;
    if (provided) {
      val = PyDict_GetItemWithError(provided, name);
      if (! val && PyErr_Occurred())
        goto fail;
    }
    if (! val) {
      val = matcher_extern_default(name);
      if (! val)
        goto fail;
    }
    Py_INCREF(val);
    PyTuple_SET_ITEM(env, i, val);
  }

  *out_env = env;
  return 0;

 fail:
  Py_DECREF(env);
  return -1;
}


/**
   Resolved extern env for a vectorcall-style entry. ``env`` is either
   the caller's value array used directly (``owned`` 0) or a buffer of
   strong refs the caller releases with matcher_env_release.
*/
typedef struct {
  PyObject *const *env;
  PyObject **buf;              /* heap buffer, or NULL when inline */
  PyObject *inline_buf[KOS_MATCH_ENV_INLINE];
  Py_ssize_t owned;            /* strong refs held in the buffer */
} MatcherEnv;


static void matcher_env_release(MatcherEnv *r) {

  PyObject **buf = r->buf ? r->buf : r->inline_buf;

  for (Py_ssize_t i = 0; i < r->owned; i++)
    Py_DECREF(buf[i]);
  PyMem_Free(r->buf);
  r->buf = NULL;
  r->owned = 0;
}


/**
   Build the extern env named by ``ext_names`` over ``ext_values``.
   Fast path: the names cover the whole extern table in order, so the
   value array is the env as-is (identity-first compares: the common
   caller feeds back the matcher's own names). General path: per table
   entry take the named value when present (order-independent, kwargs
   semantics), else the inbuilt defaults, holding one strong ref per
   entry for the run since an extern callable may mutate or reset the
   defaults dict mid-match. Returns 0, or -1 with an exception set.
*/
static int matcher_env_resolve(
    KosMatcher *m,
    PyObject *const *ext_values,
    PyObject *ext_names,
    MatcherEnv *r) {

  r->env = NULL;
  r->buf = NULL;
  r->owned = 0;

  Py_ssize_t n_named = 0;
  if (ext_names && PyTuple_Check(ext_names))
    n_named = PyTuple_GET_SIZE(ext_names);
  Py_ssize_t n_ext = PyTuple_GET_SIZE(m->externs);

  if (n_named == n_ext) {
    Py_ssize_t i = 0;
    for (; i < n_ext; i++) {
      PyObject *a = PyTuple_GET_ITEM(ext_names, i);
      PyObject *b = PyTuple_GET_ITEM(m->externs, i);
      if (a == b)
        continue;
      int eq = PyObject_RichCompareBool(a, b, Py_EQ);
      if (eq < 0)
        return -1;
      if (! eq)
        break;
    }
    if (i == n_ext) {
      r->env = ext_values;
      return 0;
    }
  }

  if (n_ext == 0) {
    PyErr_SetString(PyExc_TypeError,
                    "matcher takes no extern bindings");
    return -1;
  }

  PyObject **env = r->inline_buf;
  if (n_ext > KOS_MATCH_ENV_INLINE) {
    env = PyMem_Malloc((size_t) n_ext * sizeof(PyObject *));
    if (! env) {
      PyErr_NoMemory();
      return -1;
    }
    r->buf = env;
  }

  for (Py_ssize_t i = 0; i < n_ext; i++) {
    PyObject *name = PyTuple_GET_ITEM(m->externs, i);
    PyObject *val = NULL;
    for (Py_ssize_t j = 0; j < n_named; j++) {
      PyObject *nm = PyTuple_GET_ITEM(ext_names, j);
      if (nm != name) {
        int eq = PyObject_RichCompareBool(nm, name, Py_EQ);
        if (eq < 0)
          goto fail;
        if (! eq)
          continue;
      }
      val = ext_values[j];
      break;
    }
    if (! val) {
      val = matcher_extern_default(name);
      if (! val)
        goto fail;
    }
    Py_INCREF(val);
    env[i] = val;
    r->owned = i + 1;
  }

  r->env = env;
  return 0;

 fail:
  matcher_env_release(r);
  return -1;
}


int KosMatcher_RunVectorcall(
    KosMatcher *m,
    KosMatchSubject *subj,
    PyObject *const *ext_values,
    PyObject *ext_names,
    PyObject **out_caps) {

  if (! m) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_RunVectorcall requires matcher");
    return -1;
  }

  MatcherEnv r;
  if (matcher_env_resolve(m, ext_values, ext_names, &r))
    return -1;
  int matched = KosMatcher_Run(
      m, subj, r.env, PyTuple_GET_SIZE(m->externs), out_caps);
  matcher_env_release(&r);
  return matched;
}


int KosMatcher_RunSlotsVectorcall(
    KosMatcher *m,
    KosMatchSubject *subj,
    PyObject *const *ext_values,
    PyObject *ext_names,
    PyObject **out_slots) {

  if (! m || ! out_slots) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_RunSlotsVectorcall requires matcher "
                    "and out");
    return -1;
  }

  MatcherEnv r;
  if (matcher_env_resolve(m, ext_values, ext_names, &r)) {
    Py_ssize_t n_slots = PyTuple_GET_SIZE(m->slots);
    for (Py_ssize_t i = 0; i < n_slots; i++)
      out_slots[i] = NULL;
    return -1;
  }
  int matched = KosMatcher_RunSlots(
      m, subj, r.env, PyTuple_GET_SIZE(m->externs), out_slots);
  matcher_env_release(&r);
  return matched;
}


int KosMatcher_MatchSelect(
    PyObject *matchers,
    PyObject *subject,
    PyObject *kwargs,
    PyObject **out_result,
    Py_ssize_t *out_index) {

  if (! matchers || ! subject) {
    PyErr_SetString(PyExc_ValueError,
                    "KosMatcher_MatchSelect requires matchers and subject");
    return -1;
  }
  if (! out_result || ! out_index) {
    PyErr_SetString(PyExc_SystemError,
                    "KosMatcher_MatchSelect requires non-NULL out pointers");
    return -1;
  }
  if (kwargs != NULL && ! PyDict_Check(kwargs)) {
    PyErr_SetString(PyExc_TypeError,
                    "KosMatcher_MatchSelect kwargs must be a dict or NULL");
    return -1;
  }

  PyObject *fast = PySequence_Fast(
      matchers,
      "KosMatcher_MatchSelect matchers must be a sequence");
  if (! fast)
    return -1;

  Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
  if (n == 0) {
    Py_DECREF(fast);
    return 1;
  }

  KosMatchSubject subj;
  if (subject_from_any(subject, &subj)) {
    Py_DECREF(fast);
    return -1;
  }

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *item = PySequence_Fast_GET_ITEM(fast, i);
    if (! KosMatcher_Check(item)) {
      PyErr_Format(PyExc_TypeError,
                   "KosMatcher_MatchSelect: item %zd is not a Matcher", i);
      KosMatchSubject_Release(&subj);
      Py_DECREF(fast);
      return -1;
    }

    KosMatcher *m = (KosMatcher *) item;

    PyObject *extern_env = NULL;
    if (matcher_extern_env(m, kwargs, &extern_env)) {
      KosMatchSubject_Release(&subj);
      Py_DECREF(fast);
      return -1;
    }

    PyObject *res = KosMatcher_Match(m, &subj, extern_env);
    Py_XDECREF(extern_env);
    if (! res) {
      KosMatchSubject_Release(&subj);
      Py_DECREF(fast);
      return -1;
    }

    if (! KosMatchResult_Check(res)) {
      Py_DECREF(res);
      PyErr_SetString(PyExc_SystemError,
                      "internal: KosMatcher_Match did not return MatchResult");
      KosMatchSubject_Release(&subj);
      Py_DECREF(fast);
      return -1;
    }

    if (((KosMatchResult *) res)->matched) {
      *out_result = res;
      *out_index = i;
      KosMatchSubject_Release(&subj);
      Py_DECREF(fast);
      return 0;
    }

    Py_DECREF(res);
  }

  KosMatchSubject_Release(&subj);
  Py_DECREF(fast);
  return 1;
}


/* === Matcher type === */


static void matcher_dealloc(PyObject *self) {
  KosMatcher *m = (KosMatcher *) self;

  Py_CLEAR(m->slots);
  Py_CLEAR(m->externs);
  Py_CLEAR(m->consts);

  Py_TYPE(self)->tp_free(self);
}


static PyObject *matcher_repr(PyObject *self) {
  KosMatcher *m = (KosMatcher *) self;
  return PyUnicode_FromFormat(
      "<Matcher nodes=%zd slots=%zd externs=%zd consts=%zd entry=%u>",
      Py_SIZE(m),
      PyTuple_GET_SIZE(m->slots),
      PyTuple_GET_SIZE(m->externs),
      PyTuple_GET_SIZE(m->consts),
      (unsigned) m->entry);
}


static PyObject *kos_matcher_extern_defaults = NULL;


static int kos_matcher_populate_extern_defaults(PyObject *dict) {
  static const struct {
    const char *name;
    PyTypeObject *tp;
  } entries[] = {
    { "pair", &KosPairType },
    { "symbol", &KosSymbolType },
    { "keyword", &KosKeywordType },
    { "int", &PyLong_Type },
    { "str", &PyUnicode_Type },
    { "tuple", &PyTuple_Type },
    { "list", &PyList_Type },
  };
  const Py_ssize_t n =
      (Py_ssize_t) (sizeof(entries) / sizeof(entries[0]));

  for (Py_ssize_t i = 0; i < n; i++) {
    if (PyDict_SetItemString(dict, entries[i].name,
                             (PyObject *) entries[i].tp))
      return -1;
  }

  return 0;
}


PyObject *KosMatcher_ExternDefaults(void) {
  if (! kos_matcher_extern_defaults) {
    kos_matcher_extern_defaults = PyDict_New();
    if (! kos_matcher_extern_defaults)
      return NULL;
    if (kos_matcher_populate_extern_defaults(kos_matcher_extern_defaults)) {
      Py_CLEAR(kos_matcher_extern_defaults);
      return NULL;
    }
  }

  return kos_matcher_extern_defaults;
}


PyObject *KosMatcher_ResetExternDefaults(void) {
  Py_CLEAR(kos_matcher_extern_defaults);
  return KosMatcher_ExternDefaults();
}


static PyObject *matcher_match_method(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  KosMatcher *m = (KosMatcher *) self;
  PyObject *subject = NULL;

  if (! PyArg_ParseTuple(args, "O", &subject))
    return NULL;

  /* Build extern_env tuple aligned to m->externs from kwargs. */
  PyObject *extern_env = NULL;
  if (matcher_extern_env(m, kwargs, &extern_env))
    return NULL;

  KosMatchSubject subj;
  if (subject_from_any(subject, &subj)) {
    Py_XDECREF(extern_env);
    return NULL;
  }

  PyObject *result = KosMatcher_Match(m, &subj, extern_env);
  KosMatchSubject_Release(&subj);
  Py_XDECREF(extern_env);
  return result;
}


static PyObject *matcher_missing_externs_method(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosMatcher *m = (KosMatcher *) self;
  PyObject *provided = Py_None;
  int exclude_defaults = 0;
  static char *kwlist[] = {"provided", "exclude_defaults", NULL};

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "|Op", kwlist,
                                   &provided, &exclude_defaults))
    return NULL;

  if (provided != Py_None && ! PyMapping_Check(provided)) {
    PyErr_SetString(PyExc_TypeError,
                    "provided must be None or a mapping");
    return NULL;
  }

  Py_ssize_t n = PyTuple_GET_SIZE(m->externs);
  PyObject *lst = PyList_New(0);
  if (! lst)
    return NULL;

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *name = PyTuple_GET_ITEM(m->externs, i);
    int satisfied = 0;

    if (provided != Py_None) {
      PyObject *item = PyObject_GetItem(provided, name);
      if (item) {
        Py_DECREF(item);
        satisfied = 1;
      } else {
        if (! PyErr_ExceptionMatches(PyExc_KeyError)) {
          Py_DECREF(lst);
          return NULL;
        }
        PyErr_Clear();
      }
    }

    if (! satisfied && exclude_defaults) {
      PyObject *defs = KosMatcher_ExternDefaults();
      if (! defs) {
        Py_DECREF(lst);
        return NULL;
      }
      PyObject *const dv = PyDict_GetItemWithError(defs, name);
      if (PyErr_Occurred()) {
        Py_DECREF(lst);
        return NULL;
      }
      if (dv)
        satisfied = 1;
    }

    if (! satisfied) {
      if (PyList_Append(lst, name))
        goto fail;
    }
  }

  PyObject *tup = PyList_AsTuple(lst);
  Py_DECREF(lst);
  if (! tup)
    return NULL;
  return tup;

fail:
  Py_DECREF(lst);
  return NULL;
}


#include "matcher/thaw.c"


static PyMethodDef matcher_methods[] = {
  { "match", (PyCFunction) matcher_match_method,
    METH_VARARGS | METH_KEYWORDS,
    "match(subject, **externs) -> MatchResult" },
  { "missing_externs", (PyCFunction) matcher_missing_externs_method,
    METH_VARARGS | METH_KEYWORDS,
    "missing_externs(provided=None, *, exclude_defaults=False)\n"
    "Return extern parameter names not satisfied by provided or "
    "defaults, in extern table order" },
  { "from_tuple", (PyCFunction) matcher_from_tuple,
    METH_VARARGS | METH_CLASS,
    "Matcher.from_tuple(tagged_tuple) -> Matcher\n\n"
    "Reconstruct a Matcher from its frozen tagged-tuple "
    "representation: ('MATCHER', slots, externs, consts, nodes, "
    "entry)" },
  { NULL, NULL, 0, NULL },
};


static PyObject *matcher_get_slots(PyObject *self, void *closure) {
  (void) closure;
  PyObject *t = ((KosMatcher *) self)->slots;
  Py_INCREF(t);
  return t;
}


static PyObject *matcher_get_externs(PyObject *self, void *closure) {
  (void) closure;
  PyObject *t = ((KosMatcher *) self)->externs;
  Py_INCREF(t);
  return t;
}


static PyObject *matcher_get_consts(PyObject *self, void *closure) {
  (void) closure;
  PyObject *t = ((KosMatcher *) self)->consts;
  Py_INCREF(t);
  return t;
}


static PyObject *matcher_get_node_count(PyObject *self, void *closure) {
  (void) closure;
  return PyLong_FromSsize_t(Py_SIZE(self));
}


static PyObject *matcher_get_entry(PyObject *self, void *closure) {
  (void) closure;
  return PyLong_FromUnsignedLong(((KosMatcher *) self)->entry);
}


static PyGetSetDef matcher_getset[] = {
  { "slots", matcher_get_slots, NULL,
    "Tuple of capture slot names", NULL },
  { "externs", matcher_get_externs, NULL,
    "Tuple of extern parameter names", NULL },
  { "consts", matcher_get_consts, NULL,
    "Tuple of literal/sub-matcher consts", NULL },
  { "node_count", matcher_get_node_count, NULL,
    "Number of nodes in the matcher", NULL },
  { "entry", matcher_get_entry, NULL,
    "Index of the entry node", NULL },
  { NULL }
};


PyTypeObject KosMatcherType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Matcher",
  .tp_basicsize = offsetof(KosMatcher, nodes),
  .tp_itemsize = sizeof(KosMatchNode),
  .tp_dealloc = matcher_dealloc,
  .tp_repr = matcher_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Compiled native matcher (matcher)",
  .tp_methods = matcher_methods,
  .tp_getset = matcher_getset,
  .tp_new = NULL,
};


/* === module functions === */


static PyObject *matcher_mod_match_select(
    PyObject *mod,
    PyObject *args,
    PyObject *kwargs) {

  (void) mod;

  PyObject *matchers = NULL;
  PyObject *subject = NULL;

  if (! PyArg_ParseTuple(args, "OO", &matchers, &subject))
    return NULL;

  PyObject *res = NULL;
  Py_ssize_t idx = 0;
  int rc = KosMatcher_MatchSelect(matchers, subject, kwargs, &res, &idx);

  if (rc < 0)
    return NULL;

  if (rc > 0)
    return Py_BuildValue("(iO)", -1, Py_None);

  PyObject *pair = PyTuple_New(2);
  if (! pair) {
    Py_DECREF(res);
    return NULL;
  }
  PyObject *ilong = PyLong_FromSsize_t(idx);
  if (! ilong) {
    Py_DECREF(pair);
    Py_DECREF(res);
    return NULL;
  }
  PyTuple_SET_ITEM(pair, 0, ilong);
  PyTuple_SET_ITEM(pair, 1, res);
  return pair;
}


static PyObject *matcher_mod_get_extern_defaults(
    PyObject *mod,
    PyObject *unused) {

  (void) mod;
  (void) unused;
  PyObject *d = KosMatcher_ExternDefaults();
  if (! d)
    return NULL;
  Py_INCREF(d);
  return d;
}


static PyObject *matcher_mod_reset_extern_defaults(
    PyObject *mod,
    PyObject *unused) {

  (void) mod;
  (void) unused;
  PyObject *d = KosMatcher_ResetExternDefaults();
  if (! d)
    return NULL;
  Py_INCREF(d);
  return d;
}


static PyMethodDef matcher_mod_methods[] = {
  { "match_select", (PyCFunction) matcher_mod_match_select,
    METH_VARARGS | METH_KEYWORDS,
    "match_select(matchers, subject, /, **externs) -> (index, result)\n\n"
    "Run each Matcher in ``matchers`` in order against ``subject`` "
    "until one matches. Returns ``(index, MatchResult)`` for the first "
    "success, or ``(-1, None)`` if none match. Keyword arguments are "
    "extern bindings, same as ``Matcher.match``." },
  { "get_extern_defaults", (PyCFunction) matcher_mod_get_extern_defaults,
    METH_NOARGS,
    "Return the live matcher extern defaults dict (mutable)." },
  { "reset_extern_defaults", (PyCFunction) matcher_mod_reset_extern_defaults,
    METH_NOARGS,
    "Reset matcher extern defaults to the built-in baseline and "
    "return the new dict (mutable)." },
  { NULL, NULL, 0, NULL },
};


/* === unit lifecycle === */


int koskari_matcher_init(PyObject *mod) {
  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosMatcherType))
    return -1;

  if (PyType_Ready(&KosMatchResultType))
    return -1;

  if (PyType_Ready(&KosDatumIterType))
    return -1;

  if (PyType_Ready(&KosDatumType))
    return -1;

  if (PyType_Ready(&KosDatumFrameType))
    return -1;

  kos_match_args_name = PyUnicode_InternFromString("__match_args__");
  if (! kos_match_args_name)
    return -1;

  kos_class_name = PyUnicode_InternFromString("__class__");
  if (! kos_class_name)
    return -1;
  kos_object_class_descr = PyType_Lookup(
      &PyBaseObject_Type, kos_class_name);
  if (! kos_object_class_descr) {
    PyErr_SetString(PyExc_SystemError,
                    "object.__class__ descriptor not found");
    return -1;
  }
  Py_INCREF(kos_object_class_descr);

  if (PyDict_SetItemString(dict, "Matcher",
                           (PyObject *) &KosMatcherType))
    return -1;

  if (PyDict_SetItemString(dict, "MatchResult",
                           (PyObject *) &KosMatchResultType))
    return -1;

  if (PyDict_SetItemString(dict, "Datum",
                           (PyObject *) &KosDatumType))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(dict, "datum",
                           (PyObject *) &KosDatumType))
    return -1;

  if (PyDict_SetItemString(dict, "DatumFrame",
                           (PyObject *) &KosDatumFrameType))
    return -1;

  if (PyModule_AddFunctions(mod, matcher_mod_methods))
    return -1;

  return 0;
}


int koskari_matcher_clear(PyObject *mod) {
  (void) mod;
  Py_CLEAR(kos_matcher_extern_defaults);
  Py_CLEAR(kos_match_args_name);
  Py_CLEAR(kos_class_name);
  Py_CLEAR(kos_object_class_descr);
  return 0;
}


// The end.
