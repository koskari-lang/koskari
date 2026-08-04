/* Included by matcher.c — match_not */
/* === NOT matching === */


static int match_not(
    MatchEngine *e,
    const KosMatchNode *n,
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

// The end.
