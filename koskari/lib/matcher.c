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

  uint32_t *child_indices;
  Py_ssize_t child_count;
  Py_ssize_t child_capacity;

  PyObject *slots;     /* PyListObject during build */
  PyObject *externs;   /* PyListObject during build */
  PyObject *consts;    /* PyListObject during build */
  PyObject *meta;      /* PyDictObject or NULL */
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


static int builder_grow_children(KosMatcherBuilder *b, Py_ssize_t need) {
  if (b->child_count + need <= b->child_capacity)
    return 0;

  Py_ssize_t cap = b->child_capacity ? b->child_capacity : 8;
  while (cap < b->child_count + need)
    cap *= 2;

  uint32_t *nn = PyMem_Realloc(b->child_indices, cap * sizeof(uint32_t));
  if (! nn) {
    PyErr_NoMemory();
    return -1;
  }

  b->child_indices = nn;
  b->child_capacity = cap;
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

  case KOS_MATCH_NODE_GROUP: {
    Py_ssize_t end = (Py_ssize_t) node->as.group.first_child +
                     (Py_ssize_t) node->as.group.child_count;
    if (end > b->child_count) {
      PyErr_Format(PyExc_ValueError,
                   "group child range out of bounds: "
                   "first=%u count=%u (have %zd entries)",
                   (unsigned) node->as.group.first_child,
                   (unsigned) node->as.group.child_count,
                   b->child_count);
      return -1;
    }
    for (uint32_t i = 0; i < node->as.group.child_count; i++) {
      uint32_t cidx = b->child_indices[node->as.group.first_child + i];
      if (append_check_index(b, cidx, "group.child"))
        return -1;
    }
    break;
  }

  case KOS_MATCH_NODE_ONEOF: {
    Py_ssize_t end = (Py_ssize_t) node->as.oneof.first_child +
                     (Py_ssize_t) node->as.oneof.child_count;
    if (end > b->child_count) {
      PyErr_Format(PyExc_ValueError,
                   "oneof child range out of bounds: "
                   "first=%u count=%u (have %zd entries)",
                   (unsigned) node->as.oneof.first_child,
                   (unsigned) node->as.oneof.child_count,
                   b->child_count);
      return -1;
    }
    for (uint32_t i = 0; i < node->as.oneof.child_count; i++) {
      uint32_t cidx = b->child_indices[node->as.oneof.first_child + i];
      if (append_check_index(b, cidx, "oneof.child"))
        return -1;
    }
    break;
  }

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
  b->child_indices = NULL;
  b->child_count = 0;
  b->child_capacity = 0;
  b->slots = PyList_New(0);
  b->externs = PyList_New(0);
  b->consts = PyList_New(0);
  b->meta = NULL;

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
  PyMem_Free(b->child_indices);
  Py_XDECREF(b->slots);
  Py_XDECREF(b->externs);
  Py_XDECREF(b->consts);
  Py_XDECREF(b->meta);
  PyMem_Free(b);
}


Py_ssize_t KosMatcherBuilder_NodeCount(KosMatcherBuilder *b) {
  return b ? b->node_count : 0;
}


Py_ssize_t KosMatcherBuilder_ChildCount(KosMatcherBuilder *b) {
  return b ? b->child_count : 0;
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


int KosMatcherBuilder_AppendChildIndex(
    KosMatcherBuilder *b,
    uint32_t child_node_index) {

  if (! b) {
    PyErr_SetString(PyExc_ValueError, "AppendChildIndex requires builder");
    return -1;
  }

  if (child_node_index != KOS_MATCH_NO_INDEX &&
      (Py_ssize_t) child_node_index >= b->node_count) {
    PyErr_Format(PyExc_ValueError,
                 "child index forward reference: %u (current count=%zd)",
                 (unsigned) child_node_index, b->node_count);
    return -1;
  }

  if (builder_grow_children(b, 1))
    return -1;

  Py_ssize_t off = b->child_count;
  b->child_indices[off] = child_node_index;
  b->child_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError,
                    "matcher child index space exhausted");
    return -1;
  }

  return (int) off;
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
  /* Widths get filled by validator; force defaults for clarity. */
  b->nodes[off].min_width = 0;
  b->nodes[off].max_width = 0;
  b->node_count++;

  if (off > (Py_ssize_t) 0x7FFFFFFE) {
    PyErr_SetString(PyExc_OverflowError, "matcher node space exhausted");
    return -1;
  }

  return (int) off;
}


int KosMatcherBuilder_SetMeta(KosMatcherBuilder *b, PyObject *meta) {
  if (! b) {
    PyErr_SetString(PyExc_ValueError, "SetMeta requires builder");
    return -1;
  }

  if (meta && ! PyDict_Check(meta)) {
    PyErr_SetString(PyExc_TypeError, "meta must be a dict");
    return -1;
  }

  Py_XINCREF(meta);
  Py_XSETREF(b->meta, meta);
  return 0;
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

  case KOS_MATCH_NODE_GROUP: {
    uint32_t mn = 0, mx = 0;
    int has_unbounded = 0;
    for (uint32_t i = 0; i < n->as.group.child_count; i++) {
      uint32_t cidx = m->child_indices[n->as.group.first_child + i];
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
    for (uint32_t i = 0; i < n->as.oneof.child_count; i++) {
      uint32_t cidx = m->child_indices[n->as.oneof.first_child + i];
      uint32_t cmn = m->nodes[cidx].min_width;
      uint32_t cmx = m->nodes[cidx].max_width;
      if (cmn < mn)
        mn = cmn;
      if (cmx == KOS_MATCH_UNBOUNDED)
        has_unbounded = 1;
      else if (cmx > mx)
        mx = cmx;
    }
    if (n->as.oneof.child_count == 0) {
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
    n->min_width = 0;
    n->max_width = KOS_MATCH_UNBOUNDED;
    break;

  case KOS_MATCH_NODE_NOT:
    n->min_width = 0;
    n->max_width = 0;
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

  for (Py_ssize_t i = 0; i < m->node_count; i++) {
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

  case KOS_MATCH_NODE_GROUP:
    for (uint32_t i = 0; i < n->as.group.child_count; i++) {
      uint32_t cidx = m->child_indices[n->as.group.first_child + i];
      if (has_capture_in_subtree(m, cidx))
        return 1;
    }
    return 0;

  case KOS_MATCH_NODE_ONEOF:
    for (uint32_t i = 0; i < n->as.oneof.child_count; i++) {
      uint32_t cidx = m->child_indices[n->as.oneof.first_child + i];
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
  for (Py_ssize_t i = 0; i < m->node_count; i++) {
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
   fresh interior branches. Transparent containers (CAPTURE, OPTIONAL,
   GROUP) keep their substructure within the parent branch.

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

  case KOS_MATCH_NODE_GROUP:
    for (uint32_t i = 0; i < n->as.group.child_count; i++) {
      uint32_t cidx = m->child_indices[n->as.group.first_child + i];
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
  case KOS_MATCH_NODE_FORM: {
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

  case KOS_MATCH_NODE_GROUP:
    for (uint32_t i = 0; i < n->as.group.child_count; i++) {
      uint32_t cidx = m->child_indices[n->as.group.first_child + i];
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
    c.min = 0;
    c.max = KOS_MATCH_UNBOUNDED;
    c.is_rest = 1;
    return chunklist_push(cl, c);

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


static int validate_branches(KosMatcher *m) {
  if (m->node_count == 0)
    return 0;

  if ((Py_ssize_t) m->entry >= m->node_count) {
    PyErr_Format(PyExc_ValueError,
                 "matcher entry index %u out of range (node_count=%zd)",
                 (unsigned) m->entry, m->node_count);
    return -1;
  }

  if (validate_one_branch(m, m->entry))
    return -1;

  for (Py_ssize_t i = 0; i < m->node_count; i++) {
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
      for (uint32_t j = 0; j < n->as.oneof.child_count; j++) {
        uint32_t cidx = m->child_indices[n->as.oneof.first_child + j];
        if (validate_one_branch(m, cidx))
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

  PyObject *meta = b->meta;
  if (meta) {
    Py_INCREF(meta);
  } else {
    meta = PyDict_New();
    if (! meta) {
      Py_DECREF(slots_t);
      Py_DECREF(externs_t);
      Py_DECREF(consts_t);
      return NULL;
    }
  }

  KosMatcher *m = PyObject_New(KosMatcher, &KosMatcherType);
  if (! m) {
    Py_DECREF(slots_t);
    Py_DECREF(externs_t);
    Py_DECREF(consts_t);
    Py_DECREF(meta);
    return NULL;
  }

  m->slots = slots_t;
  m->externs = externs_t;
  m->consts = consts_t;
  m->meta = meta;
  m->nodes = NULL;
  m->node_count = 0;
  m->child_indices = NULL;
  m->child_count = 0;
  m->entry = entry;

  if (b->node_count) {
    m->nodes = PyMem_Malloc(b->node_count * sizeof(KosMatchNode));
    if (! m->nodes) {
      PyErr_NoMemory();
      Py_DECREF(m);
      return NULL;
    }
    memcpy(m->nodes, b->nodes, b->node_count * sizeof(KosMatchNode));
    m->node_count = b->node_count;
  }

  if (b->child_count) {
    m->child_indices = PyMem_Malloc(b->child_count * sizeof(uint32_t));
    if (! m->child_indices) {
      PyErr_NoMemory();
      Py_DECREF(m);
      return NULL;
    }
    memcpy(m->child_indices, b->child_indices,
           b->child_count * sizeof(uint32_t));
    m->child_count = b->child_count;
  }

  /* Phase B: range checks for slot/extern/const indices */
  if (validate_slot_extern_const_indices(m)) {
    Py_DECREF(m);
    return NULL;
  }

  /* Phase B: bottom-up width compute */
  for (Py_ssize_t i = 0; i < m->node_count; i++) {
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

  /* Phase C: per-branch validation */
  if (validate_branches(m)) {
    Py_DECREF(m);
    return NULL;
  }

  return (PyObject *) m;
}


/* === Matcher type === */


static void matcher_dealloc(PyObject *self) {
  KosMatcher *m = (KosMatcher *) self;

  PyMem_Free(m->nodes);
  m->nodes = NULL;
  PyMem_Free(m->child_indices);
  m->child_indices = NULL;

  Py_CLEAR(m->slots);
  Py_CLEAR(m->externs);
  Py_CLEAR(m->consts);
  Py_CLEAR(m->meta);

  Py_TYPE(self)->tp_free(self);
}


static PyObject *matcher_repr(PyObject *self) {
  KosMatcher *m = (KosMatcher *) self;
  return PyUnicode_FromFormat(
      "<Matcher nodes=%zd slots=%zd externs=%zd consts=%zd entry=%u>",
      m->node_count,
      PyTuple_GET_SIZE(m->slots),
      PyTuple_GET_SIZE(m->externs),
      PyTuple_GET_SIZE(m->consts),
      (unsigned) m->entry);
}


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


static PyObject *matcher_get_meta(PyObject *self, void *closure) {
  (void) closure;
  PyObject *d = ((KosMatcher *) self)->meta;
  Py_INCREF(d);
  return d;
}


static PyObject *matcher_get_node_count(PyObject *self, void *closure) {
  (void) closure;
  return PyLong_FromSsize_t(((KosMatcher *) self)->node_count);
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
  { "meta", matcher_get_meta, NULL,
    "Hygiene/provenance metadata dict", NULL },
  { "node_count", matcher_get_node_count, NULL,
    "Number of nodes in the matcher", NULL },
  { "entry", matcher_get_entry, NULL,
    "Index of the entry node", NULL },
  { NULL }
};


PyTypeObject KosMatcherType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Matcher",
  .tp_basicsize = sizeof(KosMatcher),
  .tp_itemsize = 0,
  .tp_dealloc = matcher_dealloc,
  .tp_repr = matcher_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Compiled native matcher (matcher_ng)",
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

  if (PyDict_SetItemString(dict, "Matcher",
                           (PyObject *) &KosMatcherType))
    return -1;

  return 0;
}


int koskari_matcher_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
