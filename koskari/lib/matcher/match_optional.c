/* Included by matcher.c — match_optional */
/* === OPTIONAL matching === */


static void width_optional(KosMatcher *m, KosMatchNode *n) {
  uint32_t cmx = 1;
  if (n->as.optional.subnode != KOS_MATCH_NO_INDEX)
    cmx = m->nodes[n->as.optional.subnode].max_width;
  n->min_width = 0;
  n->max_width = cmx;
}


static int match_optional(
    MatchEngine *e,
    const KosMatchNode *n,
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


// The end.
