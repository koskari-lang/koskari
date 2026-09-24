/* Included by matcher.c — match_group */
/* === GROUP matching === */


static void width_group(KosMatcher *m, KosMatchNode *n) {
  chain_width(m, n->as.group.first_child, &n->min_width, &n->max_width);
}


/**
   Run GROUP children in an isolated capture dict. On success *out_sub
   receives a new reference to the populated dict (caller must DECREF).
*/
static int match_group(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min,
    PyObject **out_sub) {

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
  PyObject *old_capture_parent = e->capture_parent;
  Py_ssize_t start = *cursor;
  PyObject *sub = match_frame_new(e->subj);
  if (! sub)
    return -1;

  e->capture_parent = e->cur_captures;
  e->cur_captures = sub;

  for (uint32_t cidx = n->as.group.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    KosMatchNode *const child = &e->m->nodes[cidx];
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
      e->capture_parent = old_capture_parent;
      engine_rollback(e, snap);
      Py_DECREF(sub);
      return r;
    }
  }

  e->cur_captures = parent_dict;
  e->capture_parent = old_capture_parent;
  if (datum_frame_set_source(sub, e->subj, start, *cursor)) {
    Py_DECREF(sub);
    return -1;
  }
  *out_sub = sub;
  return 1;
}


// The end.
