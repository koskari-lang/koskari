/* Included by matcher.c — match_map */
/* === MAP / MAP_ENTRY matching === */


/**
   Match one MAP entry against a mapping subject: resolve the key
   (const or extern), look it up, and match the entry's sub-matcher
   against the looked-up value in its own synthesized single-element
   subject. A missing key folds to a soft no-match; other lookup
   errors propagate.
*/
static int match_one_mentry(
    MatchEngine *e,
    KosMatchNode *en,
    PyObject *mapping) {

  PyObject *key;
  if (en->as.map_entry.key_kind == KOS_MATCH_MAPKEY_CONST) {
    key = PyTuple_GET_ITEM(e->m->consts, en->as.map_entry.key_ref);
  } else {
    if (! e->extern_env) {
      PyErr_SetString(PyExc_RuntimeError,
                      "MAP extern key requires an extern environment");
      return -1;
    }
    key = PyTuple_GET_ITEM(e->extern_env, en->as.map_entry.key_ref);
  }

  PyObject *val = PyObject_GetItem(mapping, key);
  if (! val) {
    if (PyErr_ExceptionMatches(PyExc_KeyError)) {
      PyErr_Clear();
      return 0;
    }
    return -1;
  }

  int r = match_projected_value(e, en->as.map_entry.subnode, val);
  Py_DECREF(val);
  return r;
}


/**
   Cached collections.abc.Mapping, resolved lazily on the first non-dict
   MAP attempt and then immortal. The MAP miss path runs for every
   non-mapping element that flows past a mapping arm, so it must not pay
   import-machinery costs per call; a benign double-resolve race under
   the GIL at worst leaks one reference. Returns a borrowed reference,
   or NULL with an exception set.
*/
static PyObject *get_abc_mapping(void) {
  static PyObject *abc_mapping_type = NULL;

  if (! abc_mapping_type) {
    PyObject *abc = PyImport_ImportModule("collections.abc");
    if (! abc)
      return NULL;
    abc_mapping_type = PyObject_GetAttrString(abc, "Mapping");
    Py_DECREF(abc);
  }
  return abc_mapping_type;
}


/**
   MAP: match the current spine element as a mapping. The element must
   be dict-like (PyDict, or an instance of collections.abc.Mapping);
   each entry's key must be present with a value matching the entry's
   sub-pattern. Extra keys are ignored, per PEP 634 mapping-pattern
   semantics.
*/
static int match_map(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
    return 0;

  PyObject *elem = e->subj->vt->get(e->subj, *cursor);
  PyObject *mapping = matcher_peel_datum(elem);

  if (! PyDict_Check(mapping)) {
    /* Not a dict: accept any collections.abc.Mapping, reject the rest
       with a soft no-match. */
    PyObject *mapping_type = get_abc_mapping();
    if (! mapping_type)
      return -1;
    int isinst = PyObject_IsInstance(mapping, mapping_type);
    if (isinst < 0)
      return -1;
    if (! isinst)
      return 0;
  }

  for (uint32_t eidx = n->as.map.first_entry;
       eidx != KOS_MATCH_NO_INDEX;
       eidx = e->m->nodes[eidx].next) {
    KosMatchNode *en = &e->m->nodes[eidx];
    int r = match_one_mentry(e, en, mapping);
    if (r != 1)
      return r;
  }

  (*cursor)++;
  return 1;
}


// The end.
