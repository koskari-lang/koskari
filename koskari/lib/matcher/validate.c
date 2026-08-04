/* Included by matcher.c — validate */
/* === width compute === */


/* Complex per-kind widths live with their kind, in the
   matcher/match_<kind>.c fragments included later in matcher.c. */
static void width_capture(KosMatcher *m, KosMatchNode *n);
static void width_match(KosMatcher *m, KosMatchNode *n);
static void width_group(KosMatcher *m, KosMatchNode *n);
static void width_oneof(KosMatcher *m, KosMatchNode *n);
static void width_repeat(KosMatcher *m, KosMatchNode *n);
static void width_optional(KosMatcher *m, KosMatchNode *n);
static void width_field(KosMatcher *m, KosMatchNode *n);


static int compute_one_width(KosMatcher *m, uint32_t idx) {
  KosMatchNode *const n = &m->nodes[idx];

  switch (n->kind) {
  case KOS_MATCH_NODE_TEST:
  case KOS_MATCH_NODE_SAME:
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_CAPTURE:
    width_capture(m, n);
    break;

  case KOS_MATCH_NODE_FORM:
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_MATCH:
    width_match(m, n);
    break;

  case KOS_MATCH_NODE_GROUP:
    width_group(m, n);
    break;

  case KOS_MATCH_NODE_ONEOF:
    width_oneof(m, n);
    break;

  case KOS_MATCH_NODE_REPEAT:
    width_repeat(m, n);
    break;

  case KOS_MATCH_NODE_OPTIONAL:
    width_optional(m, n);
    break;

  case KOS_MATCH_NODE_REST:
    n->min_width = n->as.rest.min_tail;
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

  case KOS_MATCH_NODE_APP:
    /* consumes exactly one spine element; the sub-matcher runs against
       the extractor's result in its own synthesized subject */
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_MAP:
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_MAP_ENTRY:
    /* inert on the outer spine, like FIELD: only meaningful in a MAP
       chain; report the width-1 contract of the looked-up value */
    n->min_width = 1;
    n->max_width = 1;
    break;

  case KOS_MATCH_NODE_FIELD:
    width_field(m, n);
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
    KosMatchNode *const n = &m->nodes[i];

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

    case KOS_MATCH_NODE_SAME:
      if ((Py_ssize_t) n->as.same.slot >= n_slots) {
        PyErr_Format(PyExc_ValueError,
                     "SAME slot %u out of range (slots size=%zd) "
                     "at node %zd",
                     (unsigned) n->as.same.slot, n_slots, i);
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
      break;

    case KOS_MATCH_NODE_GROUP:
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

    case KOS_MATCH_NODE_APP:
      if ((Py_ssize_t) n->as.app.fn_extern >= n_externs) {
        PyErr_Format(PyExc_ValueError,
                     "APP fn_extern %u out of range "
                     "(externs size=%zd) at node %zd",
                     (unsigned) n->as.app.fn_extern, n_externs, i);
        return -1;
      }
      break;

    case KOS_MATCH_NODE_MAP_ENTRY:
      switch (n->as.map_entry.key_kind) {
      case KOS_MATCH_MAPKEY_CONST:
        if ((Py_ssize_t) n->as.map_entry.key_ref >= n_consts) {
          PyErr_Format(PyExc_ValueError,
                       "MAP_ENTRY const key_ref %u out of range "
                       "(consts size=%zd) at node %zd",
                       (unsigned) n->as.map_entry.key_ref, n_consts, i);
          return -1;
        }
        break;
      case KOS_MATCH_MAPKEY_EXTERN:
        if ((Py_ssize_t) n->as.map_entry.key_ref >= n_externs) {
          PyErr_Format(PyExc_ValueError,
                       "MAP_ENTRY extern key_ref %u out of range "
                       "(externs size=%zd) at node %zd",
                       (unsigned) n->as.map_entry.key_ref, n_externs, i);
          return -1;
        }
        break;
      default:
        PyErr_Format(PyExc_ValueError,
                     "unknown MAP_ENTRY key kind %u at node %zd",
                     (unsigned) n->as.map_entry.key_kind, i);
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

  KosMatchNode *const n = &m->nodes[idx];

  switch (n->kind) {
  case KOS_MATCH_NODE_CAPTURE:
    return 1;

  case KOS_MATCH_NODE_TEST:
  case KOS_MATCH_NODE_SAME:
  case KOS_MATCH_NODE_REST:
    return 0;

  case KOS_MATCH_NODE_FORM:
    return has_capture_in_subtree(m, n->as.form.subnode);

  case KOS_MATCH_NODE_APP:
    return has_capture_in_subtree(m, n->as.app.subnode);

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
    KosMatchNode *const n = &m->nodes[i];

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

  KosMatchNode *const n = &m->nodes[idx];

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

  KosMatchNode *const n = &m->nodes[idx];
  BranchChunk c;

  switch (n->kind) {
  case KOS_MATCH_NODE_TEST:
  case KOS_MATCH_NODE_SAME:
  case KOS_MATCH_NODE_FORM:
  case KOS_MATCH_NODE_CLASS:
  case KOS_MATCH_NODE_APP:
  case KOS_MATCH_NODE_MAP: {
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
    KosMatchNode *const n = &m->nodes[i];
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

      KosMatchNode *const fn = &m->nodes[fidx];
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
    KosMatchNode *const n = &m->nodes[i];

    switch (n->kind) {
    case KOS_MATCH_NODE_FORM:
      if (n->as.form.subnode != KOS_MATCH_NO_INDEX &&
          validate_one_branch(m, n->as.form.subnode))
        return -1;
      break;

    case KOS_MATCH_NODE_APP:
      if (n->as.app.subnode != KOS_MATCH_NO_INDEX &&
          validate_one_branch(m, n->as.app.subnode))
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
        KosMatchNode *const fn = &m->nodes[fidx];
        if (fn->kind != KOS_MATCH_NODE_FIELD)
          break;
        if (fn->as.field.subnode != KOS_MATCH_NO_INDEX &&
            validate_one_branch(m, fn->as.field.subnode))
          return -1;
      }
      break;

    case KOS_MATCH_NODE_MAP:
      for (uint32_t eidx = n->as.map.first_entry;
           eidx != KOS_MATCH_NO_INDEX;
           eidx = m->nodes[eidx].next) {
        KosMatchNode *const en = &m->nodes[eidx];
        if (en->kind != KOS_MATCH_NODE_MAP_ENTRY)
          break;
        if (en->as.map_entry.subnode != KOS_MATCH_NO_INDEX &&
            validate_one_branch(m, en->as.map_entry.subnode))
          return -1;
      }
      break;

    default:
      break;
    }
  }

  return 0;
}


// The end.
