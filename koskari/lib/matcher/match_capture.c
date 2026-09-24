/* Included by matcher.c — match_capture */
/* === CAPTURE matching === */


static void width_capture(KosMatcher *m, KosMatchNode *n) {
  if (n->as.capture.subnode == KOS_MATCH_NO_INDEX) {
    n->min_width = 1;
    n->max_width = 1;
  } else {
    KosMatchNode *const sub = &m->nodes[n->as.capture.subnode];
    n->min_width = sub->min_width;
    n->max_width = sub->max_width;
  }
}


static int match_capture(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t start = *cursor;

  if (n->as.capture.subnode != KOS_MATCH_NO_INDEX) {
    KosMatchNode *sub = &e->m->nodes[n->as.capture.subnode];
    if (sub->kind == KOS_MATCH_NODE_REPEAT) {
      PyObject *items = NULL;
      int r = match_repeat_capture_list(
          e, sub, cursor, trailing_min, &items);
      if (r != 1)
        return r;
      if (n->as.capture.slot != KOS_MATCH_NO_INDEX) {
        if (engine_set_slot(e, n->as.capture.slot, items))
          return -1;
      } else {
        Py_DECREF(items);
      }
      return 1;
    }
    if (sub->kind == KOS_MATCH_NODE_GROUP) {
      PyObject *sub_dict = NULL;
      int r = match_group(e, sub, cursor, trailing_min, &sub_dict);
      if (r != 1)
        return r;
      if (n->as.capture.slot != KOS_MATCH_NO_INDEX) {
        if (engine_set_slot(e, n->as.capture.slot, sub_dict))
          return -1;
      } else {
        Py_DECREF(sub_dict);
      }
      return 1;
    }
    if (sub->kind == KOS_MATCH_NODE_REST) {
      Py_ssize_t rest_start = *cursor;
      int r = match_rest(e, sub, cursor, trailing_min);
      if (r != 1)
        return r;
      if (n->as.capture.slot == KOS_MATCH_NO_INDEX)
        return 1;
      Py_ssize_t end = *cursor;
      Py_ssize_t consume = end - rest_start;
      Py_ssize_t length = e->subj->vt->length(e->subj);
      PyObject *value = rest_value_for_range(
          e, rest_start, end, consume, length);
      if (! value)
        return -1;
      if (engine_set_slot(e, n->as.capture.slot, value))
        return -1;
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


// The end.
