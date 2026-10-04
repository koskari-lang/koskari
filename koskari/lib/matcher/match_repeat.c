/* Included by matcher.c — match_repeat */
/* === REPEAT matching === */


static void width_repeat(KosMatcher *m, KosMatchNode *n) {
  uint32_t sub_min = 1;
  if (n->as.repeat.subnode != KOS_MATCH_NO_INDEX)
    sub_min = m->nodes[n->as.repeat.subnode].min_width;
  uint64_t prod = (uint64_t) n->as.repeat.min_count * (uint64_t) sub_min;
  n->min_width = prod > (uint64_t) 0xFFFFFFFEu
      ? (uint32_t) 0xFFFFFFFEu
      : (uint32_t) prod;
  n->max_width = KOS_MATCH_UNBOUNDED;
}


/**
   CAPTURE(REPEAT(...)): greedily repeat inner while each iteration
   advances the cursor, honoring trailing_min on inner attempts, and
   append one Python value per iteration (anonymous GROUP dict copy,
   sparse change dict, single-cell Datum, or multi-cell slice tuple).
*/
static int match_repeat_capture_list(
    MatchEngine *e,
    KosMatchNode *rep,
    Py_ssize_t *cursor,
    uint32_t trailing_min,
    PyObject **out_items) {

  uint32_t inner_idx = rep->as.repeat.subnode;
  KosMatchNode *inner = &e->m->nodes[inner_idx];
  PyObject *items = PyList_New(0);
  if (! items)
    return -1;

  while (1) {
    Py_ssize_t snap = engine_snapshot(e);
    Py_ssize_t saved = *cursor;

    if (inner->kind == KOS_MATCH_NODE_GROUP) {
      PyObject *sub_dict = NULL;
      int r = match_group(
          e, inner, cursor, trailing_min, &sub_dict);
      if (r < 0) {
        Py_DECREF(items);
        return -1;
      }
      if (r == 0 || *cursor == saved) {
        /* a failed attempt leaves sub_dict NULL; a zero-width success
           cannot pass the REPEAT min-width validation, but if one did
           its dict is released here rather than leaked */
        Py_XDECREF(sub_dict);
        *cursor = saved;
        engine_rollback(e, snap);
        break;
      }
      if (PyDict_Size(sub_dict) == 0) {
        Py_DECREF(sub_dict);
        Py_ssize_t span = *cursor - saved;
        PyObject *elem = NULL;
        if (span == 1) {
          PyObject *raw = e->subj->vt->get(e->subj, saved);
          Py_INCREF(raw);
          elem = kos_datum_from_spine_cell(e->subj, saved, raw);
          if (! elem) {
            engine_rollback(e, snap);
            Py_DECREF(items);
            return -1;
          }
        } else if (span > 1) {
          elem = wrap_slice_datums(e->subj, saved, *cursor);
          if (! elem) {
            engine_rollback(e, snap);
            Py_DECREF(items);
            return -1;
          }
        } else {
          engine_rollback(e, snap);
          Py_DECREF(items);
          PyErr_SetString(PyExc_RuntimeError,
                          "internal: anonymous GROUP matched without "
                          "advancing cursor or captures");
          return -1;
        }
        engine_rollback(e, snap);
        if (PyList_Append(items, elem)) {
          Py_DECREF(elem);
          Py_DECREF(items);
          return -1;
        }
        Py_DECREF(elem);
        continue;
      }
      PyObject *row_copy = match_frame_copy(sub_dict);
      if (! row_copy) {
        Py_DECREF(sub_dict);
        Py_DECREF(items);
        return -1;
      }
      engine_rollback(e, snap);
      Py_DECREF(sub_dict);
      if (PyList_Append(items, row_copy)) {
        Py_DECREF(row_copy);
        Py_DECREF(items);
        return -1;
      }
      Py_DECREF(row_copy);
      continue;
    }

    int r = match_node(e, inner_idx, cursor, trailing_min);
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
    if (PyDict_Size(frame) > 0) {
      engine_rollback(e, snap);
      if (PyList_Append(items, frame)) {
        Py_DECREF(frame);
        Py_DECREF(items);
        return -1;
      }
      Py_DECREF(frame);
      continue;
    }
    Py_DECREF(frame);

    Py_ssize_t span = *cursor - saved;
    PyObject *elem = NULL;
    if (span == 1) {
      PyObject *raw = e->subj->vt->get(e->subj, saved);
      Py_INCREF(raw);
      elem = kos_datum_from_spine_cell(e->subj, saved, raw);
      if (! elem) {
        engine_rollback(e, snap);
        Py_DECREF(items);
        return -1;
      }
    } else if (span > 1) {
      elem = wrap_slice_datums(e->subj, saved, *cursor);
      if (! elem) {
        engine_rollback(e, snap);
        Py_DECREF(items);
        return -1;
      }
    } else {
      engine_rollback(e, snap);
      Py_DECREF(items);
      PyErr_SetString(PyExc_RuntimeError,
                      "internal: REPEAT iteration matched without "
                      "advancing cursor");
      return -1;
    }
    engine_rollback(e, snap);
    if (PyList_Append(items, elem)) {
      Py_DECREF(elem);
      Py_DECREF(items);
      return -1;
    }
    Py_DECREF(elem);
  }

  if (PyList_GET_SIZE(items) < (Py_ssize_t) rep->as.repeat.min_count) {
    Py_DECREF(items);
    return 0;
  }

  *out_items = items;
  return 1;
}


static int match_repeat(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  uint32_t inner = n->as.repeat.subnode;
  uint32_t got = 0;
  while (1) {
    Py_ssize_t snap = engine_snapshot(e);
    Py_ssize_t saved = *cursor;
    int r = match_node(e, inner, cursor, trailing_min);
    if (r < 0)
      return -1;
    if (r == 0 || *cursor == saved) {
      *cursor = saved;
      engine_rollback(e, snap);
      break;
    }
    got++;
  }
  if (got < n->as.repeat.min_count)
    return 0;
  return 1;
}

// The end.
