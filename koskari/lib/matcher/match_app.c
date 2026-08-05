/* Included by matcher.c — match_app */
/* === APP matching === */


/**
   APP: call the extractor extern on the current spine element and match
   the sub-pattern against the call's result. The extractor is invoked
   through the demand path (a plain koskari callable works directly; a
   genuine async suspension raises KoskariSyncViolation); a raising
   extractor propagates as an error, never a no-match. The result is
   matched in its own synthesized single-element subject, same as a
   CLASS field's projected value.
*/
static int match_app(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
    return 0;

  if (! e->extern_env) {
    PyErr_SetString(PyExc_RuntimeError,
                    "APP requires an extern environment");
    return -1;
  }

  PyObject *elem = e->subj->vt->get(e->subj, *cursor);
  PyObject *fn = e->extern_env[n->as.app.fn_extern];
  PyObject *got = matcher_peel_datum(elem);

  PyObject *res = matcher_call_extern(fn, got);
  if (! res)
    return -1;

  int r = match_projected_value(e, n->as.app.subnode, res);
  Py_DECREF(res);
  if (r != 1)
    return r;

  (*cursor)++;
  return 1;
}


// The end.
