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
   matcher_ng — native low-level matcher core (Phase 1)

   Implements the tagged-node array, C-internal builder, and validator
   for the new matcher engine. The matching engine and subject adapters
   land in Phase 2; the frozen tagged-tuple thaw lands in Phase 3.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GNU General Public License v3
   ai-assistant: Claude Opus 4.7 via Cursor
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
                 "use AppendMatch, AppendNamedGroup, or AppendOneof");
    return -1;

  case KOS_MATCH_NODE_CLASS:
  case KOS_MATCH_NODE_FIELD:
    PyErr_Format(PyExc_ValueError,
                 "AppendNode does not accept CLASS or FIELD kinds; "
                 "use AppendClass / AppendFieldPositional / "
                 "AppendFieldKeyword");
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
    /* slot is range-checked at finalize */
    break;

  case KOS_MATCH_NODE_NOT:
    if (append_check_index(b, node->as.not_.subnode, "not.subnode"))
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
    uint32_t group_slot,
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
    n.as.group.slot = group_slot;
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
      b, KOS_MATCH_NODE_MATCH, 0, children, count, "AppendMatch");
}


int KosMatcherBuilder_AppendNamedGroup(
    KosMatcherBuilder *b,
    uint32_t slot,
    const uint32_t *children,
    Py_ssize_t count) {

  if (slot == KOS_MATCH_NO_INDEX) {
    PyErr_SetString(PyExc_ValueError,
                    "AppendNamedGroup requires a valid slot index");
    return -1;
  }

  return builder_append_chain_container(
      b, KOS_MATCH_NODE_GROUP, slot, children, count, "AppendNamedGroup");
}


int KosMatcherBuilder_AppendOneof(
    KosMatcherBuilder *b,
    const uint32_t *branches,
    Py_ssize_t count) {

  return builder_append_chain_container(
      b, KOS_MATCH_NODE_ONEOF, 0, branches, count, "AppendOneof");
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
    KosMatchNode *fn = &b->nodes[fidx];
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

  for (Py_ssize_t i = 0; i + 1 < count; i++)
    b->nodes[fields[i]].next = fields[i + 1];

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
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


/* === width compute === */


static int compute_one_width(KosMatcher *m, uint32_t idx) {
  KosMatchNode *n = &m->nodes[idx];

  switch (n->kind) {
  case KOS_MATCH_NODE_TEST:
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_CAPTURE:
    if (n->as.capture.subnode == KOS_MATCH_NO_INDEX) {
      n->min_width = 1;
      n->max_width = 1;
    } else {
      KosMatchNode *sub = &m->nodes[n->as.capture.subnode];
      n->min_width = sub->min_width;
      n->max_width = sub->max_width;
    }
    break;

  case KOS_MATCH_NODE_FORM:
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_MATCH:
  case KOS_MATCH_NODE_GROUP: {
    uint32_t head = (n->kind == KOS_MATCH_NODE_MATCH)
        ? n->as.match.first_child
        : n->as.group.first_child;
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
    n->min_width = mn;
    n->max_width = has_unbounded ? KOS_MATCH_UNBOUNDED : mx;
    break;
  }

  case KOS_MATCH_NODE_ONEOF: {
    uint32_t mn = KOS_MATCH_UNBOUNDED, mx = 0;
    int has_unbounded = 0;
    int has_branch = 0;
    for (uint32_t cidx = n->as.oneof.first_branch;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      has_branch = 1;
      uint32_t cmn = m->nodes[cidx].min_width;
      uint32_t cmx = m->nodes[cidx].max_width;
      if (cmn < mn)
        mn = cmn;
      if (cmx == KOS_MATCH_UNBOUNDED)
        has_unbounded = 1;
      else if (cmx > mx)
        mx = cmx;
    }
    if (! has_branch) {
      mn = 0;
      mx = 0;
    }
    n->min_width = mn;
    n->max_width = has_unbounded ? KOS_MATCH_UNBOUNDED : mx;
    break;
  }

  case KOS_MATCH_NODE_REPEAT:
    n->min_width = 0;
    n->max_width = KOS_MATCH_UNBOUNDED;
    break;

  case KOS_MATCH_NODE_OPTIONAL: {
    uint32_t cmx = 1;
    if (n->as.optional.subnode != KOS_MATCH_NO_INDEX)
      cmx = m->nodes[n->as.optional.subnode].max_width;
    n->min_width = 0;
    n->max_width = cmx;
    break;
  }

  case KOS_MATCH_NODE_REST:
    n->min_width = 1;
    n->max_width = KOS_MATCH_UNBOUNDED;
    break;

  case KOS_MATCH_NODE_NOT:
    n->min_width = 0;
    n->max_width = 0;
    break;

  case KOS_MATCH_NODE_CLASS:
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_FIELD:
    /* FIELDs are inert as outer-spine atoms; only meaningful as
       descendants of a CLASS chain. The structural validator rejects
       any FIELD reachable as a parent-spine branch node. We still
       provide a width here so width-compute over the array does not
       fault: report the inner subnode's width-1 contract. */
    if (n->as.field.subnode == KOS_MATCH_NO_INDEX) {
      n->min_width = 1;
      n->max_width = 1;
    } else {
      KosMatchNode *sub = &m->nodes[n->as.field.subnode];
      n->min_width = sub->min_width;
      n->max_width = sub->max_width;
    }
    break;

  default:
    PyErr_Format(PyExc_ValueError,
                 "unknown node kind during width compute: %u",
                 (unsigned) n->kind);
    return -1;
  }

  return 0;
}


/* === finalize-time index validation === */


static int validate_slot_extern_const_indices(KosMatcher *m) {
  Py_ssize_t n_slots = PyTuple_GET_SIZE(m->slots);
  Py_ssize_t n_externs = PyTuple_GET_SIZE(m->externs);
  Py_ssize_t n_consts = PyTuple_GET_SIZE(m->consts);

  for (Py_ssize_t i = 0; i < Py_SIZE(m); i++) {
    KosMatchNode *n = &m->nodes[i];

    switch (n->kind) {
    case KOS_MATCH_NODE_TEST:
      switch (n->as.test.test_kind) {
      case KOS_MATCH_TEST_ANY:
        break;
      case KOS_MATCH_TEST_LITERAL:
        if ((Py_ssize_t) n->as.test.ref_index >= n_consts) {
          PyErr_Format(PyExc_ValueError,
                       "TEST_LITERAL ref_index %u out of range "
                       "(consts size=%zd) at node %zd",
                       (unsigned) n->as.test.ref_index, n_consts, i);
          return -1;
        }
        break;
      case KOS_MATCH_TEST_TYPE:
      case KOS_MATCH_TEST_PREDICATE:
        if ((Py_ssize_t) n->as.test.ref_index >= n_externs) {
          PyErr_Format(PyExc_ValueError,
                       "TEST type/predicate ref_index %u out of range "
                       "(externs size=%zd) at node %zd",
                       (unsigned) n->as.test.ref_index, n_externs, i);
          return -1;
        }
        break;
      default:
        PyErr_Format(PyExc_ValueError,
                     "unknown TEST kind %u at node %zd",
                     (unsigned) n->as.test.test_kind, i);
        return -1;
      }
      break;

    case KOS_MATCH_NODE_CAPTURE:
      if (n->as.capture.slot != KOS_MATCH_NO_INDEX &&
          (Py_ssize_t) n->as.capture.slot >= n_slots) {
        PyErr_Format(PyExc_ValueError,
                     "CAPTURE slot %u out of range (slots size=%zd) "
                     "at node %zd",
                     (unsigned) n->as.capture.slot, n_slots, i);
        return -1;
      }
      if (n->as.capture.scope_slot != KOS_MATCH_NO_INDEX &&
          (Py_ssize_t) n->as.capture.scope_slot >= n_slots) {
        PyErr_Format(PyExc_ValueError,
                     "CAPTURE scope_slot %u out of range "
                     "(slots size=%zd) at node %zd",
                     (unsigned) n->as.capture.scope_slot, n_slots, i);
        return -1;
      }
      break;

    case KOS_MATCH_NODE_REST:
      if (n->as.rest.slot != KOS_MATCH_NO_INDEX &&
          (Py_ssize_t) n->as.rest.slot >= n_slots) {
        PyErr_Format(PyExc_ValueError,
                     "REST slot %u out of range (slots size=%zd) "
                     "at node %zd",
                     (unsigned) n->as.rest.slot, n_slots, i);
        return -1;
      }
      break;

    case KOS_MATCH_NODE_GROUP:
      if (n->as.group.slot != KOS_MATCH_NO_INDEX &&
          (Py_ssize_t) n->as.group.slot >= n_slots) {
        PyErr_Format(PyExc_ValueError,
                     "GROUP slot %u out of range (slots size=%zd) "
                     "at node %zd",
                     (unsigned) n->as.group.slot, n_slots, i);
        return -1;
      }
      break;

    case KOS_MATCH_NODE_CLASS:
      if ((Py_ssize_t) n->as.class_.type_extern >= n_externs) {
        PyErr_Format(PyExc_ValueError,
                     "CLASS type_extern %u out of range "
                     "(externs size=%zd) at node %zd",
                     (unsigned) n->as.class_.type_extern, n_externs, i);
        return -1;
      }
      break;

    case KOS_MATCH_NODE_FIELD:
      switch (n->as.field.field_kind) {
      case KOS_MATCH_FIELD_POSITIONAL:
        if (n->as.field.name_const != KOS_MATCH_NO_INDEX) {
          PyErr_Format(PyExc_ValueError,
                       "FIELD positional at node %zd must have "
                       "name_const == NO_INDEX", i);
          return -1;
        }
        break;
      case KOS_MATCH_FIELD_KEYWORD: {
        if ((Py_ssize_t) n->as.field.name_const >= n_consts) {
          PyErr_Format(PyExc_ValueError,
                       "FIELD keyword name_const %u out of range "
                       "(consts size=%zd) at node %zd",
                       (unsigned) n->as.field.name_const, n_consts, i);
          return -1;
        }
        PyObject *cv =
            PyTuple_GET_ITEM(m->consts, n->as.field.name_const);
        if (! PyUnicode_Check(cv)) {
          PyErr_Format(PyExc_TypeError,
                       "FIELD keyword name_const at node %zd refers to "
                       "a non-str const (got %.200s)",
                       i, Py_TYPE(cv)->tp_name);
          return -1;
        }
        break;
      }
      default:
        PyErr_Format(PyExc_ValueError,
                     "FIELD field_kind %u out of range at node %zd",
                     (unsigned) n->as.field.field_kind, i);
        return -1;
      }
      break;

    default:
      break;
    }
  }

  return 0;
}


/* === REPEAT min-width and CAPTURE-in-NOT validation === */


static int has_capture_in_subtree(KosMatcher *m, uint32_t idx) {
  if (idx == KOS_MATCH_NO_INDEX)
    return 0;

  KosMatchNode *n = &m->nodes[idx];

  switch (n->kind) {
  case KOS_MATCH_NODE_CAPTURE:
    return 1;

  case KOS_MATCH_NODE_TEST:
  case KOS_MATCH_NODE_REST:
    return 0;

  case KOS_MATCH_NODE_FORM:
    return has_capture_in_subtree(m, n->as.form.subnode);

  case KOS_MATCH_NODE_MATCH:
    for (uint32_t cidx = n->as.match.first_child;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (has_capture_in_subtree(m, cidx))
        return 1;
    }
    return 0;

  case KOS_MATCH_NODE_GROUP:
    for (uint32_t cidx = n->as.group.first_child;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (has_capture_in_subtree(m, cidx))
        return 1;
    }
    return 0;

  case KOS_MATCH_NODE_ONEOF:
    for (uint32_t cidx = n->as.oneof.first_branch;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (has_capture_in_subtree(m, cidx))
        return 1;
    }
    return 0;

  case KOS_MATCH_NODE_REPEAT:
    return has_capture_in_subtree(m, n->as.repeat.subnode);

  case KOS_MATCH_NODE_OPTIONAL:
    return has_capture_in_subtree(m, n->as.optional.subnode);

  case KOS_MATCH_NODE_NOT:
    return has_capture_in_subtree(m, n->as.not_.subnode);

  default:
    return 0;
  }
}


static int validate_repeat_and_not(KosMatcher *m) {
  for (Py_ssize_t i = 0; i < Py_SIZE(m); i++) {
    KosMatchNode *n = &m->nodes[i];

    if (n->kind == KOS_MATCH_NODE_REPEAT) {
      if (n->as.repeat.subnode == KOS_MATCH_NO_INDEX) {
        PyErr_Format(PyExc_ValueError,
                     "REPEAT at node %zd has no subnode", i);
        return -1;
      }
      uint32_t sub_min = m->nodes[n->as.repeat.subnode].min_width;
      if (sub_min < 1) {
        PyErr_Format(PyExc_ValueError,
                     "REPEAT subnode at node %zd must consume at least "
                     "one element (min_width=%u)",
                     i, (unsigned) sub_min);
        return -1;
      }
    }

    if (n->kind == KOS_MATCH_NODE_NOT) {
      if (n->as.not_.subnode != KOS_MATCH_NO_INDEX &&
          has_capture_in_subtree(m, n->as.not_.subnode)) {
        PyErr_Format(PyExc_ValueError,
                     "NOT at node %zd contains a CAPTURE in its subtree",
                     i);
        return -1;
      }
    }
  }

  return 0;
}


/* === branch validation ===
   A "branch" is a linear sequence of nodes operating on a single spine
   cursor. Branch boundaries (FORM, ONEOF children, REPEAT, NOT) start
   fresh interior branches.    Transparent containers (CAPTURE, OPTIONAL,
   MATCH, GROUP) keep their substructure within the parent branch.

   For each branch we enforce:
     - at most one REST (across all transparent descendants)
     - every linearized chunk after the REST has min_width == max_width
*/


typedef struct {
  uint32_t min;
  uint32_t max;
  int is_rest;
} BranchChunk;


typedef struct {
  BranchChunk *items;
  Py_ssize_t count;
  Py_ssize_t capacity;
} ChunkList;


static void chunklist_init(ChunkList *cl) {
  cl->items = NULL;
  cl->count = 0;
  cl->capacity = 0;
}


static void chunklist_free(ChunkList *cl) {
  PyMem_Free(cl->items);
  cl->items = NULL;
  cl->count = 0;
  cl->capacity = 0;
}


static int chunklist_push(ChunkList *cl, BranchChunk c) {
  if (cl->count == cl->capacity) {
    Py_ssize_t cap = cl->capacity ? cl->capacity * 2 : 8;
    BranchChunk *nn = PyMem_Realloc(cl->items, cap * sizeof(BranchChunk));
    if (! nn) {
      PyErr_NoMemory();
      return -1;
    }
    cl->items = nn;
    cl->capacity = cap;
  }

  cl->items[cl->count++] = c;
  return 0;
}


/**
   Detect whether the subtree rooted at idx contains a REST node within
   the same branch (descending only through transparent containers
   CAPTURE, OPTIONAL, GROUP). FORM/ONEOF/REPEAT/NOT introduce sub-branch
   boundaries and are not descended.
*/
static int subtree_has_rest(KosMatcher *m, uint32_t idx) {
  if (idx == KOS_MATCH_NO_INDEX)
    return 0;

  KosMatchNode *n = &m->nodes[idx];

  switch (n->kind) {
  case KOS_MATCH_NODE_REST:
    return 1;

  case KOS_MATCH_NODE_CAPTURE:
    return subtree_has_rest(m, n->as.capture.subnode);

  case KOS_MATCH_NODE_OPTIONAL:
    return subtree_has_rest(m, n->as.optional.subnode);

  case KOS_MATCH_NODE_MATCH:
    for (uint32_t cidx = n->as.match.first_child;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (subtree_has_rest(m, cidx))
        return 1;
    }
    return 0;

  case KOS_MATCH_NODE_GROUP:
    for (uint32_t cidx = n->as.group.first_child;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (subtree_has_rest(m, cidx))
        return 1;
    }
    return 0;

  default:
    return 0;
  }
}


static int collect_chunks(KosMatcher *m, uint32_t idx, ChunkList *cl) {
  if (idx == KOS_MATCH_NO_INDEX)
    return 0;

  KosMatchNode *n = &m->nodes[idx];
  BranchChunk c;

  switch (n->kind) {
  case KOS_MATCH_NODE_TEST:
  case KOS_MATCH_NODE_FORM:
  case KOS_MATCH_NODE_CLASS: {
    c.min = 1;
    c.max = 1;
    c.is_rest = 0;
    return chunklist_push(cl, c);
  }

  case KOS_MATCH_NODE_CAPTURE:
    if (n->as.capture.subnode == KOS_MATCH_NO_INDEX) {
      c.min = 1;
      c.max = 1;
      c.is_rest = 0;
      return chunklist_push(cl, c);
    }
    return collect_chunks(m, n->as.capture.subnode, cl);

  case KOS_MATCH_NODE_MATCH:
    for (uint32_t cidx = n->as.match.first_child;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (collect_chunks(m, cidx, cl))
        return -1;
    }
    return 0;

  case KOS_MATCH_NODE_GROUP:
    for (uint32_t cidx = n->as.group.first_child;
         cidx != KOS_MATCH_NO_INDEX;
         cidx = m->nodes[cidx].next) {
      if (collect_chunks(m, cidx, cl))
        return -1;
    }
    return 0;

  case KOS_MATCH_NODE_ONEOF:
  case KOS_MATCH_NODE_REPEAT:
  case KOS_MATCH_NODE_NOT: {
    c.min = n->min_width;
    c.max = n->max_width;
    c.is_rest = 0;
    return chunklist_push(cl, c);
  }

  case KOS_MATCH_NODE_OPTIONAL: {
    /* OPTIONAL is atomic for chunk emission: its on/off decision means
       the parent branch sees a single (0, child.max) contribution. If
       the subtree contains a REST, the OPTIONAL also acts as that
       branch's REST-bearer (mark is_rest=1 so the count is correct and
       so any post-OPTIONAL nodes are validated against the deterministic
       suffix rule). */
    int has_rest = 0;
    if (n->as.optional.subnode != KOS_MATCH_NO_INDEX)
      has_rest = subtree_has_rest(m, n->as.optional.subnode);
    c.min = 0;
    c.max = n->max_width;
    c.is_rest = has_rest;
    return chunklist_push(cl, c);
  }

  case KOS_MATCH_NODE_REST:
    c.min = n->min_width;
    c.max = KOS_MATCH_UNBOUNDED;
    c.is_rest = 1;
    return chunklist_push(cl, c);

  case KOS_MATCH_NODE_FIELD:
    PyErr_Format(PyExc_ValueError,
                 "FIELD at node %u may not appear on a spine branch",
                 (unsigned) idx);
    return -1;

  default:
    PyErr_Format(PyExc_ValueError,
                 "unknown node kind during branch chunk collection: %u",
                 (unsigned) n->kind);
    return -1;
  }
}


static int validate_one_branch(KosMatcher *m, uint32_t idx) {
  ChunkList cl;
  chunklist_init(&cl);

  if (collect_chunks(m, idx, &cl)) {
    chunklist_free(&cl);
    return -1;
  }

  int rest_count = 0;
  Py_ssize_t first_rest = -1;

  for (Py_ssize_t i = 0; i < cl.count; i++) {
    if (cl.items[i].is_rest) {
      rest_count++;
      if (first_rest < 0)
        first_rest = i;
    }
  }

  if (rest_count > 1) {
    PyErr_Format(PyExc_ValueError,
                 "branch at node %u has %d REST nodes (max 1 per branch)",
                 (unsigned) idx, rest_count);
    chunklist_free(&cl);
    return -1;
  }

  if (first_rest >= 0) {
    for (Py_ssize_t i = first_rest + 1; i < cl.count; i++) {
      if (cl.items[i].min != cl.items[i].max) {
        PyErr_Format(PyExc_ValueError,
                     "branch at node %u has non-deterministic chunk after "
                     "REST (chunk %zd: min=%u max=%u)",
                     (unsigned) idx, i,
                     (unsigned) cl.items[i].min,
                     (unsigned) cl.items[i].max);
        chunklist_free(&cl);
        return -1;
      }
    }
  }

  chunklist_free(&cl);
  return 0;
}


/**
   Structural validation of CLASS / FIELD nodes:

     - every FIELD is on exactly one CLASS chain
     - every node on a CLASS chain is a FIELD (the chain itself may
       only consist of KOS_MATCH_NODE_FIELD nodes)
     - chain order has all positional FIELDs before any keyword FIELD
     - each FIELD's subnode (the projection sub-matcher) has
       min_width == max_width == 1
*/
static int validate_class_and_field_structure(KosMatcher *m) {
  Py_ssize_t total = Py_SIZE(m);
  if (total == 0)
    return 0;

  uint8_t *seen = PyMem_Calloc(total, sizeof(uint8_t));
  if (! seen) {
    PyErr_NoMemory();
    return -1;
  }

  for (Py_ssize_t i = 0; i < total; i++) {
    KosMatchNode *n = &m->nodes[i];
    if (n->kind != KOS_MATCH_NODE_CLASS)
      continue;

    int saw_keyword = 0;
    for (uint32_t fidx = n->as.class_.first_field;
         fidx != KOS_MATCH_NO_INDEX;
         fidx = m->nodes[fidx].next) {

      if ((Py_ssize_t) fidx >= total) {
        PyErr_Format(PyExc_ValueError,
                     "CLASS at node %zd has out-of-range field index %u",
                     i, (unsigned) fidx);
        PyMem_Free(seen);
        return -1;
      }

      KosMatchNode *fn = &m->nodes[fidx];
      if (fn->kind != KOS_MATCH_NODE_FIELD) {
        PyErr_Format(PyExc_ValueError,
                     "CLASS at node %zd references non-FIELD node %u",
                     i, (unsigned) fidx);
        PyMem_Free(seen);
        return -1;
      }

      if (seen[fidx]) {
        PyErr_Format(PyExc_ValueError,
                     "FIELD at node %u participates in more than one "
                     "CLASS chain", (unsigned) fidx);
        PyMem_Free(seen);
        return -1;
      }
      seen[fidx] = 1;

      if (fn->as.field.field_kind == KOS_MATCH_FIELD_KEYWORD) {
        saw_keyword = 1;
      } else if (fn->as.field.field_kind == KOS_MATCH_FIELD_POSITIONAL) {
        if (saw_keyword) {
          PyErr_Format(PyExc_ValueError,
                       "CLASS at node %zd has positional FIELD %u after "
                       "a keyword FIELD",
                       i, (unsigned) fidx);
          PyMem_Free(seen);
          return -1;
        }
      }

      if (fn->as.field.subnode != KOS_MATCH_NO_INDEX) {
        KosMatchNode *sub = &m->nodes[fn->as.field.subnode];
        if (sub->min_width != 1 || sub->max_width != 1) {
          PyErr_Format(PyExc_ValueError,
                       "FIELD at node %u subnode must have width "
                       "exactly 1 (got min=%u max=%u)",
                       (unsigned) fidx,
                       (unsigned) sub->min_width,
                       (unsigned) sub->max_width);
          PyMem_Free(seen);
          return -1;
        }
      }
    }
  }

  for (Py_ssize_t i = 0; i < total; i++) {
    if (m->nodes[i].kind == KOS_MATCH_NODE_FIELD && ! seen[i]) {
      PyErr_Format(PyExc_ValueError,
                   "FIELD at node %zd is not referenced by any CLASS",
                   i);
      PyMem_Free(seen);
      return -1;
    }
  }

  PyMem_Free(seen);
  return 0;
}


static int validate_branches(KosMatcher *m) {
  if (Py_SIZE(m) == 0)
    return 0;

  if ((Py_ssize_t) m->entry >= Py_SIZE(m)) {
    PyErr_Format(PyExc_ValueError,
                 "matcher entry index %u out of range (node_count=%zd)",
                 (unsigned) m->entry, Py_SIZE(m));
    return -1;
  }

  if (validate_one_branch(m, m->entry))
    return -1;

  for (Py_ssize_t i = 0; i < Py_SIZE(m); i++) {
    KosMatchNode *n = &m->nodes[i];

    switch (n->kind) {
    case KOS_MATCH_NODE_FORM:
      if (n->as.form.subnode != KOS_MATCH_NO_INDEX &&
          validate_one_branch(m, n->as.form.subnode))
        return -1;
      break;

    case KOS_MATCH_NODE_REPEAT:
      if (n->as.repeat.subnode != KOS_MATCH_NO_INDEX &&
          validate_one_branch(m, n->as.repeat.subnode))
        return -1;
      break;

    case KOS_MATCH_NODE_NOT:
      if (n->as.not_.subnode != KOS_MATCH_NO_INDEX &&
          validate_one_branch(m, n->as.not_.subnode))
        return -1;
      break;

    case KOS_MATCH_NODE_ONEOF:
      for (uint32_t cidx = n->as.oneof.first_branch;
           cidx != KOS_MATCH_NO_INDEX;
           cidx = m->nodes[cidx].next) {
        if (validate_one_branch(m, cidx))
          return -1;
      }
      break;

    case KOS_MATCH_NODE_CLASS:
      for (uint32_t fidx = n->as.class_.first_field;
           fidx != KOS_MATCH_NO_INDEX;
           fidx = m->nodes[fidx].next) {
        KosMatchNode *fn = &m->nodes[fidx];
        if (fn->kind != KOS_MATCH_NODE_FIELD)
          break;
        if (fn->as.field.subnode != KOS_MATCH_NO_INDEX &&
            validate_one_branch(m, fn->as.field.subnode))
          return -1;
      }
      break;

    default:
      break;
    }
  }

  return 0;
}


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


/* === Datum === */


static void datum_dealloc(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  Py_CLEAR(d->datum);
  Py_CLEAR(d->position);
  Py_CLEAR(d->binding);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *datum_repr(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  return PyUnicode_FromFormat(
      "<Datum datum=%R position=%R binding=%R>",
      d->datum, d->position, d->binding);
}


static PyObject *datum_get_datum(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->datum);
  return d->datum;
}


static PyObject *datum_get_position(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->position);
  return d->position;
}


static PyObject *datum_get_binding(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->binding);
  return d->binding;
}


static PyObject *datum_get_car(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetAttrString(d->datum, "car");
}


static PyObject *datum_get_cdr(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetAttrString(d->datum, "cdr");
}


static PyObject *datum_iter(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetIter(d->datum);
}


static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding);


static PyObject *datum_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  PyObject *datum;
  PyObject *position = Py_None;
  PyObject *binding = Py_None;
  static char *kwlist[] = {"datum", "position", "binding", NULL};

  (void) type;
  if (! PyArg_ParseTupleAndKeywords(
          args, kwds, "O|OO:Datum", kwlist,
          &datum, &position, &binding))
    return NULL;

  return kos_datum_new(datum, position, binding);
}


static PyGetSetDef datum_getset[] = {
  { "datum", datum_get_datum, NULL,
    "Matched value", NULL },
  { "position", datum_get_position, NULL,
    "Source position from enclosing pair cell, or None", NULL },
  { "binding", datum_get_binding, NULL,
    "Capture slot name (str) or None", NULL },
  { "car", datum_get_car, NULL,
    "Underlying datum car (delegates to pair)", NULL },
  { "cdr", datum_get_cdr, NULL,
    "Underlying datum cdr (delegates to pair)", NULL },
  { NULL }
};


PyTypeObject KosDatumType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Datum",
  .tp_basicsize = sizeof(KosDatum),
  .tp_dealloc = datum_dealloc,
  .tp_repr = datum_repr,
  .tp_iter = datum_iter,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Matched value with optional KosPair source position and "
            "capture binding name",
  .tp_getset = datum_getset,
  .tp_new = datum_tp_new,
};


static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding) {

  KosDatum *d = PyObject_New(KosDatum, &KosDatumType);
  if (! d)
    return NULL;
  if (! binding)
    binding = Py_None;
  Py_INCREF(datum);
  Py_INCREF(position);
  Py_INCREF(binding);
  d->datum = datum;
  d->position = position;
  d->binding = binding;
  return (PyObject *) d;
}


/**
   Set KosDatum.binding to slot name (in-place). Recurse into tuples of
   Datum (slice captures). ``name`` is the capture key (borrowed ok;
   we incref when storing).
*/
static void kos_datum_annotate_binding(PyObject *value, PyObject *name) {
  Py_ssize_t i;
  Py_ssize_t n;

  if (! value || ! name)
    return;
  if (KosDatum_Check(value)) {
    KosDatum *d = (KosDatum *) value;
    Py_INCREF(name);
    PyObject *old_b = d->binding;
    d->binding = name;
    Py_DECREF(old_b);
    return;
  }
  if (PyTuple_Check(value)) {
    n = PyTuple_GET_SIZE(value);
    for (i = 0; i < n; i++)
      kos_datum_annotate_binding(PyTuple_GET_ITEM(value, i), name);
  }
}


/* === MatchResult === */


static void match_result_dealloc(PyObject *self) {
  KosMatchResult *r = (KosMatchResult *) self;
  Py_CLEAR(r->captures);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *match_result_repr(PyObject *self) {
  KosMatchResult *r = (KosMatchResult *) self;
  Py_ssize_t nslots = r->captures ? PyDict_Size(r->captures) : 0;
  return PyUnicode_FromFormat(
      "<MatchResult matched=%s captures=%zd>",
      r->matched ? "True" : "False", nslots);
}


static int match_result_bool(PyObject *self) {
  return ((KosMatchResult *) self)->matched ? 1 : 0;
}


static PyNumberMethods match_result_as_number = {
  .nb_bool = match_result_bool,
};


static PyObject *match_result_get_matched(PyObject *self, void *closure) {
  (void) closure;
  return PyBool_FromLong(((KosMatchResult *) self)->matched);
}


static PyObject *match_result_get_captures(PyObject *self, void *closure) {
  (void) closure;
  PyObject *d = ((KosMatchResult *) self)->captures;
  Py_INCREF(d);
  return d;
}


static PyGetSetDef match_result_getset[] = {
  { "matched", match_result_get_matched, NULL,
    "True if the matcher matched the subject", NULL },
  { "captures", match_result_get_captures, NULL,
    "dict[str, value] of captured slot values", NULL },
  { NULL }
};


PyTypeObject KosMatchResultType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.MatchResult",
  .tp_basicsize = sizeof(KosMatchResult),
  .tp_dealloc = match_result_dealloc,
  .tp_repr = match_result_repr,
  .tp_as_number = &match_result_as_number,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Result of running a matcher against a subject",
  .tp_getset = match_result_getset,
};


static PyObject *make_match_result(
    int matched,
    PyObject *captures) {

  KosMatchResult *r = PyObject_New(KosMatchResult, &KosMatchResultType);
  if (! r)
    return NULL;
  Py_INCREF(captures);
  r->captures = captures;
  r->matched = matched;
  return (PyObject *) r;
}


/* === subject adapters === */


static Py_ssize_t pyseq_length(KosMatchSubject *s) {
  return s->cached_length;
}


static PyObject *pyseq_get(KosMatchSubject *s, Py_ssize_t i) {
  return PyTuple_GET_ITEM(s->backing, i);
}


static PyObject *pyseq_tail(KosMatchSubject *s, Py_ssize_t start) {
  return PyTuple_GetSlice(s->backing, start, s->cached_length);
}


static PyObject *pyseq_slice(
    KosMatchSubject *s,
    Py_ssize_t start,
    Py_ssize_t end) {

  return PyTuple_GetSlice(s->backing, start, end);
}


static void pyseq_release(KosMatchSubject *s) {
  Py_CLEAR(s->backing);
  s->cached_length = 0;
}


static const KosMatchSubjectVTable pyseq_vtable = {
  .length = pyseq_length,
  .get = pyseq_get,
  .tail = pyseq_tail,
  .slice = pyseq_slice,
  .release = pyseq_release,
};


int KosMatchSubject_FromPySequence(PyObject *seq, KosMatchSubject *out) {
  if (! seq || ! out) {
    PyErr_SetString(PyExc_ValueError,
                    "FromPySequence requires seq and out");
    return -1;
  }

  PyObject *fast = PySequence_Tuple(seq);
  if (! fast)
    return -1;

  out->vt = &pyseq_vtable;
  out->backing = fast;
  out->kp_cars = NULL;
  out->kp_tails = NULL;
  out->cached_length = PyTuple_GET_SIZE(fast);
  return 0;
}


static Py_ssize_t kospair_length(KosMatchSubject *s) {
  return s->cached_length;
}


static PyObject *kospair_get(KosMatchSubject *s, Py_ssize_t i) {
  return s->kp_cars[i];
}


static PyObject *kospair_tail(KosMatchSubject *s, Py_ssize_t start) {
  PyObject *t = s->kp_tails[start];
  Py_INCREF(t);
  return t;
}


static PyObject *kospair_slice(
    KosMatchSubject *s,
    Py_ssize_t start,
    Py_ssize_t end) {

  /* Build a fresh tuple of car values for the half-open range
     [start, end). Slice semantics are tuple-shaped because the engine
     uses slices only for multi-element CAPTURE values, not for REST. */
  Py_ssize_t n = end - start;
  if (n < 0)
    n = 0;
  PyObject *t = PyTuple_New(n);
  if (! t)
    return NULL;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *v = s->kp_cars[start + i];
    Py_INCREF(v);
    PyTuple_SET_ITEM(t, i, v);
  }
  return t;
}


static void kospair_release(KosMatchSubject *s) {
  Py_CLEAR(s->backing);
  PyMem_Free(s->kp_cars);
  PyMem_Free(s->kp_tails);
  s->kp_cars = NULL;
  s->kp_tails = NULL;
  s->cached_length = 0;
}


static const KosMatchSubjectVTable kospair_vtable = {
  .length = kospair_length,
  .get = kospair_get,
  .tail = kospair_tail,
  .slice = kospair_slice,
  .release = kospair_release,
};


int KosMatchSubject_FromKosPair(
    PyObject *pair_or_nil,
    KosMatchSubject *out) {

  if (! pair_or_nil || ! out) {
    PyErr_SetString(PyExc_ValueError,
                    "FromKosPair requires pair_or_nil and out");
    return -1;
  }

  /* First pass: count length */
  Py_ssize_t n = 0;
  PyObject *cursor = pair_or_nil;
  while (KosPair_CheckExact(cursor)) {
    n++;
    cursor = KosPair_CDR(cursor);
  }
  if (! KosNil_Check(cursor)) {
    PyErr_SetString(PyExc_TypeError,
                    "FromKosPair requires a proper list (nil-terminated)");
    return -1;
  }

  PyObject **cars = NULL;
  PyObject **tails = NULL;
  if (n > 0) {
    cars = PyMem_Malloc(n * sizeof(PyObject *));
    if (! cars) {
      PyErr_NoMemory();
      return -1;
    }
  }
  /* tails has n+1 entries: tails[i] = pair-at-position-i,
     tails[n] = KosNil (the final nil cell). */
  tails = PyMem_Malloc((n + 1) * sizeof(PyObject *));
  if (! tails) {
    PyMem_Free(cars);
    PyErr_NoMemory();
    return -1;
  }

  cursor = pair_or_nil;
  for (Py_ssize_t i = 0; i < n; i++) {
    tails[i] = cursor;
    cars[i] = KosPair_CAR(cursor);
    cursor = KosPair_CDR(cursor);
  }
  tails[n] = cursor;  /* KosNil */

  out->vt = &kospair_vtable;
  Py_INCREF(pair_or_nil);
  out->backing = pair_or_nil;
  out->kp_cars = cars;
  out->kp_tails = tails;
  out->cached_length = n;
  return 0;
}


/**
   Build a subject from an arbitrary Python value: KosPair / nil takes
   the KosPair adapter; anything else falls through to PySequence.
   Returns 0 on success, -1 on error. Out subject must be released
   with KosMatchSubject_Release.
*/
static PyObject *matcher_peel_datum(PyObject *obj) {
  while (obj && KosDatum_Check(obj)) {
    KosDatum *d = (KosDatum *) obj;
    obj = d->datum;
  }
  return obj;
}


static int subject_from_any(PyObject *value, KosMatchSubject *out) {
  /* Reader output often wraps pair-tree cells in Datum. Iterating a pair as
     a Python sequence yields (car, cdr), which breaks FORM on a single list
     cell such as ((t 1)): the matcher would see two spine elements and
     fail to consume the trailing nil. Peel Datum layers so KosPair lists
     use the proper-list spine adapter. */
  value = matcher_peel_datum(value);
  if (KosPair_Check(value) || KosNil_Check(value))
    return KosMatchSubject_FromKosPair(value, out);
  return KosMatchSubject_FromPySequence(value, out);
}


void KosMatchSubject_Release(KosMatchSubject *s) {
  if (! s || ! s->vt)
    return;
  s->vt->release(s);
  s->vt = NULL;
}


/* === match engine === */


typedef struct {
  PyObject *dict;
  uint32_t slot_idx;
  int had_old;
  PyObject *old_value;
} DictChange;


typedef struct {
  KosMatcher *m;
  KosMatchSubject *subj;
  PyObject *extern_env;       /* PyTuple aligned to m->externs, or NULL */
  PyObject *root_captures;
  PyObject *cur_captures;
  Py_ssize_t slot_count;
  DictChange *change_stack;
  Py_ssize_t change_count;
  Py_ssize_t change_capacity;
} MatchEngine;


static PyObject *kos_datum_from_spine_cell(
    KosMatchSubject *subj,
    Py_ssize_t cell_idx,
    PyObject *datum_owned) {

  PyObject *pos = Py_None;
  Py_INCREF(Py_None);
  if (subj->kp_tails && cell_idx >= 0
      && cell_idx < subj->cached_length) {
    PyObject *cell = subj->kp_tails[cell_idx];
    if (KosPair_CheckExact(cell)) {
      PyObject *p = KosPair_POSITION(cell);
      Py_DECREF(Py_None);
      pos = p ? p : Py_None;
      Py_INCREF(pos);
    }
  }
  PyObject *out = kos_datum_new(datum_owned, pos, Py_None);
  Py_DECREF(datum_owned);
  Py_DECREF(pos);
  return out;
}


static PyObject *wrap_slice_datums(
    KosMatchSubject *subj,
    Py_ssize_t start,
    Py_ssize_t end) {

  Py_ssize_t n = end - start;
  if (n < 0)
    n = 0;
  PyObject *t = PyTuple_New(n);
  if (! t)
    return NULL;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *raw = subj->vt->get(subj, start + i);
    Py_INCREF(raw);
    PyObject *cell = kos_datum_from_spine_cell(subj, start + i, raw);
    if (! cell) {
      Py_DECREF(t);
      return NULL;
    }
    PyTuple_SET_ITEM(t, i, cell);
  }
  return t;
}


static int engine_grow_changes(MatchEngine *e, Py_ssize_t need) {
  if (e->change_count + need <= e->change_capacity)
    return 0;
  Py_ssize_t cap = e->change_capacity ? e->change_capacity * 2 : 16;
  while (cap < e->change_count + need)
    cap *= 2;
  DictChange *nn = PyMem_Realloc(e->change_stack,
                                 cap * sizeof(DictChange));
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
  PyObject *d = e->cur_captures;

  PyObject *prev = PyDict_GetItemWithError(d, name);
  if (! prev && PyErr_Occurred()) {
    Py_DECREF(value);
    return -1;
  }

  if (engine_grow_changes(e, 1)) {
    Py_DECREF(value);
    return -1;
  }

  DictChange *ch = &e->change_stack[e->change_count];
  ch->dict = d;
  Py_INCREF(d);
  ch->slot_idx = slot;
  if (prev) {
    ch->had_old = 1;
    ch->old_value = prev;
    Py_INCREF(prev);
  } else {
    ch->had_old = 0;
    ch->old_value = NULL;
  }
  e->change_count++;

  if (PyDict_SetItem(d, name, value)) {
    Py_DECREF(value);
    return -1;
  }
  kos_datum_annotate_binding(value, name);
  Py_DECREF(value);
  return 0;
}


static Py_ssize_t engine_snapshot(MatchEngine *e) {
  return e->change_count;
}


static void engine_rollback(MatchEngine *e, Py_ssize_t snapshot) {
  while (e->change_count > snapshot) {
    e->change_count--;
    DictChange *c = &e->change_stack[e->change_count];
    PyObject *name = PyTuple_GET_ITEM(e->m->slots, c->slot_idx);
    if (! c->had_old) {
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
    Py_DECREF(c->dict);
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
    PyObject *cls = PyTuple_GET_ITEM(e->extern_env, t->ref_index);
    PyObject *got = matcher_peel_datum(elem);
    int r = PyObject_IsInstance(got, cls);
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
    PyObject *pred = PyTuple_GET_ITEM(e->extern_env, t->ref_index);
    PyObject *got = matcher_peel_datum(elem);
    PyObject *res = PyObject_CallOneArg(pred, got);
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
    PyObject *val = PyDict_GetItemWithError(e->cur_captures, name);
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


static int match_form(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
    return 0;

  PyObject *elem = e->subj->vt->get(e->subj, *cursor);

  KosMatchSubject sub;
  if (subject_from_any(elem, &sub)) {
    /* Not a sequence-like subject — match fails (no exception). */
    PyErr_Clear();
    return 0;
  }

  /* Save engine subject and replace with sub-subject for the inner
     branch. */
  KosMatchSubject *saved = e->subj;
  e->subj = &sub;

  Py_ssize_t sub_cursor = 0;
  int r = match_node(e, n->as.form.subnode, &sub_cursor, 0);

  Py_ssize_t sub_length = sub.vt->length(&sub);
  e->subj = saved;
  KosMatchSubject_Release(&sub);

  if (r != 1)
    return r;

  if (sub_cursor != sub_length)
    return 0;

  (*cursor)++;
  return 1;
}


static int match_capture(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t start = *cursor;

  /* Special-case CAPTURE wrapping REPEAT: per-iteration sub-frames
     accumulate into a list. */
  if (n->as.capture.subnode != KOS_MATCH_NO_INDEX) {
    KosMatchNode *sub = &e->m->nodes[n->as.capture.subnode];
    if (sub->kind == KOS_MATCH_NODE_REPEAT) {
      PyObject *items = PyList_New(0);
      if (! items)
        return -1;

      uint32_t inner = sub->as.repeat.subnode;
      while (1) {
        Py_ssize_t snap = engine_snapshot(e);
        Py_ssize_t saved = *cursor;
        int r = match_node(e, inner, cursor, 0);
        if (r < 0) {
          Py_DECREF(items);
          return -1;
        }
        if (r == 0 || *cursor == saved) {
          *cursor = saved;
          engine_rollback(e, snap);
          break;
        }
        PyObject *frame = engine_changes_to_dict(e, snap);
        if (! frame) {
          Py_DECREF(items);
          return -1;
        }
        if (PyList_Append(items, frame)) {
          Py_DECREF(frame);
          Py_DECREF(items);
          return -1;
        }
        Py_DECREF(frame);
        engine_rollback(e, snap);
      }

      if (n->as.capture.slot != KOS_MATCH_NO_INDEX) {
        if (engine_set_slot(e, n->as.capture.slot, items))
          return -1;
      } else {
        Py_DECREF(items);
      }
      return 1;
    }
  }

  /* Generic CAPTURE: run subnode (or any-1 fallback) and capture the
     consumed range. */
  if (n->as.capture.subnode == KOS_MATCH_NO_INDEX) {
    Py_ssize_t length = e->subj->vt->length(e->subj);
    if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
      return 0;
    (*cursor)++;
  } else {
    int r = match_node(e, n->as.capture.subnode, cursor, trailing_min);
    if (r != 1)
      return r;
  }

  Py_ssize_t end = *cursor;

  if (n->as.capture.slot == KOS_MATCH_NO_INDEX)
    return 1;

  if (end - start == 0)
    return 1;

  PyObject *value = NULL;
  if (end - start == 1) {
    PyObject *raw = e->subj->vt->get(e->subj, start);
    Py_INCREF(raw);
    value = kos_datum_from_spine_cell(e->subj, start, raw);
    if (! value)
      return -1;
  } else {
    value = wrap_slice_datums(e->subj, start, end);
    if (! value)
      return -1;
  }

  if (engine_set_slot(e, n->as.capture.slot, value))
    return -1;
  return 1;
}


static int match_match(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  /* Sum of min_widths of all children — we drain it as we iterate to
     compute each child's trailing. UNBOUNDED min would mean a child
     reserves arbitrary spine; treat as 0 for trailing accounting (the
     validator already enforces post-REST suffixes are deterministic). */
  uint32_t remaining = 0;
  for (uint32_t cidx = n->as.match.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    uint32_t cm = e->m->nodes[cidx].min_width;
    if (cm != KOS_MATCH_UNBOUNDED)
      remaining = saturating_add(remaining, cm);
  }

  for (uint32_t cidx = n->as.match.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    KosMatchNode *child = &e->m->nodes[cidx];
    uint32_t child_min = child->min_width;
    if (child_min == KOS_MATCH_UNBOUNDED)
      child_min = 0;
    if (remaining >= child_min)
      remaining -= child_min;
    else
      remaining = 0;
    uint32_t child_trailing = saturating_add(remaining, trailing_min);
    int r = match_node(e, cidx, cursor, child_trailing);
    if (r != 1)
      return r;
  }
  return 1;
}


static int match_named_group(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  uint32_t remaining = 0;
  for (uint32_t cidx = n->as.group.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    uint32_t cm = e->m->nodes[cidx].min_width;
    if (cm != KOS_MATCH_UNBOUNDED)
      remaining = saturating_add(remaining, cm);
  }

  Py_ssize_t snap = engine_snapshot(e);
  PyObject *parent_dict = e->cur_captures;
  PyObject *sub = PyDict_New();
  if (! sub)
    return -1;

  e->cur_captures = sub;

  for (uint32_t cidx = n->as.group.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    KosMatchNode *child = &e->m->nodes[cidx];
    uint32_t child_min = child->min_width;
    if (child_min == KOS_MATCH_UNBOUNDED)
      child_min = 0;
    if (remaining >= child_min)
      remaining -= child_min;
    else
      remaining = 0;
    uint32_t child_trailing = saturating_add(remaining, trailing_min);
    int r = match_node(e, cidx, cursor, child_trailing);
    if (r != 1) {
      e->cur_captures = parent_dict;
      engine_rollback(e, snap);
      Py_DECREF(sub);
      return r;
    }
  }

  e->cur_captures = parent_dict;
  if (engine_set_slot(e, n->as.group.slot, sub)) {
    engine_rollback(e, snap);
    return -1;
  }
  return 1;
}


static int match_oneof(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  for (uint32_t cidx = n->as.oneof.first_branch;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    Py_ssize_t snap = engine_snapshot(e);
    Py_ssize_t saved = *cursor;
    int r = match_node(e, cidx, cursor, trailing_min);
    if (r == 1)
      return 1;
    if (r < 0)
      return -1;
    *cursor = saved;
    engine_rollback(e, snap);
  }
  return 0;
}


static int match_repeat(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor) {

  uint32_t inner = n->as.repeat.subnode;
  while (1) {
    Py_ssize_t snap = engine_snapshot(e);
    Py_ssize_t saved = *cursor;
    int r = match_node(e, inner, cursor, 0);
    if (r < 0)
      return -1;
    if (r == 0 || *cursor == saved) {
      *cursor = saved;
      engine_rollback(e, snap);
      break;
    }
  }
  return 1;
}


static int match_optional(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t snap = engine_snapshot(e);
  Py_ssize_t saved = *cursor;
  int r = match_node(e, n->as.optional.subnode, cursor, trailing_min);
  if (r == 1)
    return 1;
  if (r < 0)
    return -1;
  *cursor = saved;
  engine_rollback(e, snap);
  return 1;
}


static int match_rest(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  Py_ssize_t available = length - *cursor;
  if (available < (Py_ssize_t) trailing_min)
    return 0;

  Py_ssize_t consume = available - (Py_ssize_t) trailing_min;
  Py_ssize_t end = *cursor + consume;

  if (consume < 1)
    return 0;

  if (n->as.rest.slot != KOS_MATCH_NO_INDEX) {
    PyObject *value;
    if (end == length) {
      value = e->subj->vt->tail(e->subj, *cursor);
      if (! value)
        return -1;
      value = kos_datum_from_spine_cell(e->subj, *cursor, value);
      if (! value)
        return -1;
    } else {
      value = wrap_slice_datums(e->subj, *cursor, end);
      if (! value)
        return -1;
    }
    if (engine_set_slot(e, n->as.rest.slot, value))
      return -1;
  }

  *cursor = end;
  return 1;
}


static int match_not(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor) {

  Py_ssize_t snap = engine_snapshot(e);
  Py_ssize_t saved = *cursor;
  int r = match_node(e, n->as.not_.subnode, cursor, 0);
  *cursor = saved;
  engine_rollback(e, snap);
  if (r < 0)
    return -1;
  return r ? 0 : 1;
}


/**
   Run a single FIELD against an instance. value resolution failure
   (AttributeError) is turned into a soft no-match (return 0); any
   other exception (or sub-matcher error) propagates as -1.

   match_args is the runtime-resolved __match_args__ tuple, or NULL
   if the CLASS chain has no positional FIELDs. pos is the running
   positional counter, advanced when this FIELD is positional.
*/
static int match_one_field(
    MatchEngine *e,
    KosMatchNode *fn,
    PyObject *elem,
    PyObject *match_args,
    Py_ssize_t *pos) {

  PyObject *attr_name = NULL;
  if (fn->as.field.field_kind == KOS_MATCH_FIELD_POSITIONAL) {
    attr_name = PyTuple_GET_ITEM(match_args, *pos);
    if (! PyUnicode_Check(attr_name)) {
      PyErr_Format(PyExc_TypeError,
                   "__match_args__[%zd] must be str (got %.200s)",
                   *pos, Py_TYPE(attr_name)->tp_name);
      return -1;
    }
    (*pos)++;
  } else {
    attr_name = PyTuple_GET_ITEM(e->m->consts, fn->as.field.name_const);
  }

  PyObject *val = PyObject_GetAttr(elem, attr_name);
  if (! val) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return 0;
    }
    return -1;
  }

  if (fn->as.field.subnode == KOS_MATCH_NO_INDEX) {
    Py_DECREF(val);
    return 1;
  }

  /* The FIELD's sub-matcher is width-1 — synthesize a single-element
     subject around the projected value rather than treating val itself
     as a sequence. */
  PyObject *one = PyTuple_Pack(1, val);
  Py_DECREF(val);
  if (! one)
    return -1;

  KosMatchSubject sub;
  if (KosMatchSubject_FromPySequence(one, &sub)) {
    Py_DECREF(one);
    return -1;
  }
  Py_DECREF(one);

  KosMatchSubject *saved = e->subj;
  e->subj = &sub;

  Py_ssize_t sub_cursor = 0;
  int r = match_node(e, fn->as.field.subnode, &sub_cursor, 0);

  Py_ssize_t sub_length = sub.vt->length(&sub);
  e->subj = saved;
  KosMatchSubject_Release(&sub);

  if (r != 1)
    return r;
  if (sub_cursor != sub_length)
    return 0;
  return 1;
}


static int match_class(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
    return 0;

  if (! e->extern_env) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLASS requires an extern environment");
    return -1;
  }

  PyObject *elem = e->subj->vt->get(e->subj, *cursor);
  PyObject *cls = PyTuple_GET_ITEM(e->extern_env, n->as.class_.type_extern);

  int isinst = PyObject_IsInstance(elem, cls);
  if (isinst < 0)
    return -1;
  if (! isinst)
    return 0;

  Py_ssize_t num_positional = 0;
  for (uint32_t fidx = n->as.class_.first_field;
       fidx != KOS_MATCH_NO_INDEX;
       fidx = e->m->nodes[fidx].next) {
    KosMatchNode *fn = &e->m->nodes[fidx];
    if (fn->as.field.field_kind == KOS_MATCH_FIELD_POSITIONAL)
      num_positional++;
  }

  PyObject *match_args = NULL;
  if (num_positional > 0) {
    match_args = PyObject_GetAttrString(cls, "__match_args__");
    if (! match_args) {
      if (PyErr_ExceptionMatches(PyExc_AttributeError))
        PyErr_Clear();
      else
        return -1;
      PyErr_Format(PyExc_TypeError,
                   "%R has no __match_args__ but %zd positional field "
                   "pattern(s) were supplied",
                   cls, num_positional);
      return -1;
    }
    if (! PyTuple_Check(match_args)) {
      PyErr_Format(PyExc_TypeError,
                   "%R.__match_args__ must be a tuple (got %.200s)",
                   cls, Py_TYPE(match_args)->tp_name);
      Py_DECREF(match_args);
      return -1;
    }
    if (PyTuple_GET_SIZE(match_args) < num_positional) {
      PyErr_Format(PyExc_TypeError,
                   "%R.__match_args__ has %zd entries; cannot satisfy "
                   "%zd positional field pattern(s)",
                   cls, PyTuple_GET_SIZE(match_args), num_positional);
      Py_DECREF(match_args);
      return -1;
    }
  }

  Py_ssize_t pos = 0;
  for (uint32_t fidx = n->as.class_.first_field;
       fidx != KOS_MATCH_NO_INDEX;
       fidx = e->m->nodes[fidx].next) {
    KosMatchNode *fn = &e->m->nodes[fidx];
    int r = match_one_field(e, fn, elem, match_args, &pos);
    if (r != 1) {
      Py_XDECREF(match_args);
      return r;
    }
  }

  Py_XDECREF(match_args);
  (*cursor)++;
  return 1;
}


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

  case KOS_MATCH_NODE_CAPTURE:
    return match_capture(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_FORM:
    return match_form(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_MATCH:
    return match_match(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_GROUP:
    return match_named_group(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_ONEOF:
    return match_oneof(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_REPEAT:
    return match_repeat(e, n, cursor);

  case KOS_MATCH_NODE_OPTIONAL:
    return match_optional(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_REST:
    return match_rest(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_NOT:
    return match_not(e, n, cursor);

  case KOS_MATCH_NODE_CLASS:
    return match_class(e, n, cursor, trailing_min);

  case KOS_MATCH_NODE_FIELD:
    PyErr_Format(PyExc_RuntimeError,
                 "FIELD node may not be matched directly (must be "
                 "reached via a CLASS chain)");
    return -1;

  default:
    PyErr_Format(PyExc_RuntimeError,
                 "unknown matcher node kind: %u", (unsigned) n->kind);
    return -1;
  }
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
  }

  Py_ssize_t n_slots = PyTuple_GET_SIZE(m->slots);

  PyObject *root = PyDict_New();
  if (! root)
    return NULL;

  MatchEngine engine = {0};
  engine.m = m;
  engine.subj = subj;
  engine.extern_env = extern_env;
  engine.slot_count = n_slots;
  engine.root_captures = root;
  engine.cur_captures = root;

  Py_ssize_t cursor = 0;
  int r = -1;

  if (Py_SIZE(m) > 0)
    r = match_node(&engine, m->entry, &cursor, 0);
  else
    r = 1;

  Py_ssize_t length = subj->vt->length(subj);
  int matched = 0;
  if (r == 1 && cursor == length)
    matched = 1;

  engine_discard_pending_change_refs(&engine);
  PyMem_Free(engine.change_stack);

  if (r < 0) {
    Py_DECREF(root);
    return NULL;
  }

  PyObject *out_caps;
  if (matched) {
    out_caps = root;
  } else {
    Py_DECREF(root);
    out_caps = PyDict_New();
    if (! out_caps)
      return NULL;
  }

  PyObject *result = make_match_result(matched, out_caps);
  Py_DECREF(out_caps);
  return result;
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
  Py_ssize_t n_externs = PyTuple_GET_SIZE(m->externs);
  if (n_externs > 0) {
    extern_env = PyTuple_New(n_externs);
    if (! extern_env)
      return NULL;
    for (Py_ssize_t i = 0; i < n_externs; i++) {
      PyObject *name = PyTuple_GET_ITEM(m->externs, i);
      PyObject *val = NULL;
      if (kwargs)
        val = PyDict_GetItemWithError(kwargs, name);
      if (! val) {
        if (PyErr_Occurred()) {
          Py_DECREF(extern_env);
          return NULL;
        }
        PyObject *defs = KosMatcher_ExternDefaults();
        if (! defs) {
          Py_DECREF(extern_env);
          return NULL;
        }
        val = PyDict_GetItemWithError(defs, name);
        if (! val) {
          if (PyErr_Occurred()) {
            Py_DECREF(extern_env);
            return NULL;
          }
          PyErr_Format(PyExc_TypeError,
                       "matcher missing extern binding: %R", name);
          Py_DECREF(extern_env);
          return NULL;
        }
      }
      Py_INCREF(val);
      PyTuple_SET_ITEM(extern_env, i, val);
    }
  } else if (kwargs && PyDict_GET_SIZE(kwargs) > 0) {
    /* Allow extra kwargs to be silently ignored? Reject for now to
       catch typos. */
    PyErr_SetString(PyExc_TypeError,
                    "matcher takes no extern bindings");
    return NULL;
  }

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
      PyObject *dv = PyDict_GetItemWithError(defs, name);
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
  n.as.capture.scope_slot = KOS_MATCH_NO_INDEX;
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
    if (arity != 5) {
      PyErr_Format(PyExc_ValueError,
                   "CAPTURE node tuple must be 5-tuple "
                   "(kind, slot, subnode, hygiene_role, scope_slot), "
                   "got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_CAPTURE;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.capture.slot, "CAPTURE.slot"))
      return -1;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 2),
                         &n.as.capture.subnode, "CAPTURE.subnode"))
      return -1;
    PyObject *hr = PyTuple_GET_ITEM(t, 3);
    if (! PyLong_Check(hr)) {
      PyErr_SetString(PyExc_TypeError,
                      "CAPTURE.hygiene_role must be int");
      return -1;
    }
    long hr_v = PyLong_AsLong(hr);
    if (hr_v < 0 || hr_v > 0xFFFF) {
      PyErr_SetString(PyExc_ValueError,
                      "CAPTURE.hygiene_role out of uint16 range");
      return -1;
    }
    n.as.capture.hygiene_role = (uint16_t) hr_v;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 4),
                         &n.as.capture.scope_slot, "CAPTURE.scope_slot"))
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

  } else if (PyUnicode_CompareWithASCIIString(tag, "MATCH") == 0) {
    Py_ssize_t cnt = arity - 1;
    if (cnt < 0) {
      PyErr_SetString(PyExc_ValueError, "MATCH node must have arity >= 1");
      return -1;
    }
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
    if (arity < 2) {
      PyErr_Format(PyExc_ValueError,
                   "GROUP node must be (GROUP, slot, child...), got %zd",
                   arity);
      return -1;
    }
    uint32_t gslot;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1), &gslot, "GROUP.slot"))
      return -1;
    Py_ssize_t cnt = arity - 2;
    uint32_t *children = NULL;
    if (cnt > 0) {
      children = PyMem_Malloc(cnt * sizeof(uint32_t));
      if (! children) {
        PyErr_NoMemory();
        return -1;
      }
    }
    for (Py_ssize_t i = 0; i < cnt; i++) {
      if (thaw_parse_index(PyTuple_GET_ITEM(t, 2 + i), &children[i],
                           "GROUP child")) {
        PyMem_Free(children);
        return -1;
      }
    }
    int rc = KosMatcherBuilder_AppendNamedGroup(b, gslot, children, cnt);
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
    if (arity != 2) {
      PyErr_Format(PyExc_ValueError,
                   "REPEAT node tuple must be 2-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_REPEAT;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.repeat.subnode, "REPEAT.subnode"))
      return -1;

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
                   "REST node tuple must be 2-tuple, got %zd", arity);
      return -1;
    }
    n.kind = KOS_MATCH_NODE_REST;
    if (thaw_parse_index(PyTuple_GET_ITEM(t, 1),
                         &n.as.rest.slot, "REST.slot"))
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


static PyObject *matcher_from_tuple(
    PyObject *cls,
    PyObject *args) {

  (void) cls;
  PyObject *t;
  if (! PyArg_ParseTuple(args, "O", &t))
    return NULL;
  return thaw_matcher_tuple(t);
}


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
  .tp_doc = "Compiled native matcher (matcher_ng)",
  .tp_methods = matcher_methods,
  .tp_getset = matcher_getset,
  .tp_new = NULL,
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

  if (PyType_Ready(&KosDatumType))
    return -1;

  if (PyDict_SetItemString(dict, "Matcher",
                           (PyObject *) &KosMatcherType))
    return -1;

  if (PyDict_SetItemString(dict, "MatchResult",
                           (PyObject *) &KosMatchResultType))
    return -1;

  if (PyDict_SetItemString(dict, "Datum",
                           (PyObject *) &KosDatumType))
    return -1;

  return 0;
}


int koskari_matcher_clear(PyObject *mod) {
  (void) mod;
  Py_CLEAR(kos_matcher_extern_defaults);
  return 0;
}


// The end.
