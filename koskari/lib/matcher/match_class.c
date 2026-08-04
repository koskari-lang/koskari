/* Included by matcher.c — match_class */
/* === CLASS / FIELD matching === */


/**
   FIELDs are inert as outer-spine atoms; only meaningful as
   descendants of a CLASS chain. The structural validator rejects
   any FIELD reachable as a parent-spine branch node. We still
   provide a width here so width-compute over the array does not
   fault: report the inner subnode's width-1 contract.
*/
static void width_field(KosMatcher *m, KosMatchNode *n) {
  if (n->as.field.subnode == KOS_MATCH_NO_INDEX) {
    n->min_width = 1;
    n->max_width = 1;
  } else {
    KosMatchNode *const sub = &m->nodes[n->as.field.subnode];
    n->min_width = sub->min_width;
    n->max_width = sub->max_width;
  }
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

  int r = match_projected_value(e, fn->as.field.subnode, val);
  Py_DECREF(val);
  return r;
}


#if PY_MINOR_VERSION < 10
/**
   Python 3.9 predates __match_args__ (PEP 634 added it, and dataclass
   / namedtuple generation of it, in 3.10). Fill that one API gap
   predictably: synthesize the 3.10-equivalent tuple for a dataclass
   (its init fields, via the public dataclasses.fields) or a namedtuple
   (_fields). Gated to < 3.10 only — on newer Pythons an absent
   __match_args__ on a dataclass means @dataclass(match_args=False),
   a deliberate opt-out this fallback must never override.

   Returns a new tuple reference; NULL with no exception set when cls
   is neither shape (the caller raises its usual TypeError); NULL with
   an exception set on internal failure.
*/
static PyObject *synth_match_args_39(PyObject *cls) {
  static PyObject *dc_fields = NULL;

  int is_dc = PyObject_HasAttrString(cls, "__dataclass_fields__");
  if (is_dc) {
    if (! dc_fields) {
      PyObject *dc = PyImport_ImportModule("dataclasses");
      if (! dc)
        return NULL;
      dc_fields = PyObject_GetAttrString(dc, "fields");
      Py_DECREF(dc);
      if (! dc_fields)
        return NULL;
    }

    PyObject *flds = PyObject_CallOneArg(dc_fields, cls);
    if (! flds)
      return NULL;
    PyObject *fast = PySequence_Fast(
        flds, "dataclasses.fields must return a sequence");
    Py_DECREF(flds);
    if (! fast)
      return NULL;

    PyObject *names = PyList_New(0);
    if (! names) {
      Py_DECREF(fast);
      return NULL;
    }

    Py_ssize_t count = PySequence_Fast_GET_SIZE(fast);
    for (Py_ssize_t i = 0; i < count; i++) {
      PyObject *fld = PySequence_Fast_GET_ITEM(fast, i);
      PyObject *init = PyObject_GetAttrString(fld, "init");
      if (! init)
        goto dc_fail;
      int wanted = PyObject_IsTrue(init);
      Py_DECREF(init);
      if (wanted < 0)
        goto dc_fail;
      if (! wanted)
        continue;
      PyObject *name = PyObject_GetAttrString(fld, "name");
      if (! name)
        goto dc_fail;
      int rc = PyList_Append(names, name);
      Py_DECREF(name);
      if (rc < 0)
        goto dc_fail;
    }

    Py_DECREF(fast);
    PyObject *result = PyList_AsTuple(names);
    Py_DECREF(names);
    return result;

  dc_fail:
    Py_DECREF(fast);
    Py_DECREF(names);
    return NULL;
  }

  PyObject *nt_fields = PyObject_GetAttrString(cls, "_fields");
  if (! nt_fields) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError))
      PyErr_Clear();
    return NULL;
  }
  if (PyTuple_Check(nt_fields))
    return nt_fields;
  Py_DECREF(nt_fields);
  return NULL;
}
#endif


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
#if PY_MINOR_VERSION < 10
      /* 3.9: no generated __match_args__ exists anywhere — synthesize
         the 3.10-equivalent for dataclasses and namedtuples. */
      match_args = synth_match_args_39(cls);
      if (! match_args && PyErr_Occurred())
        return -1;
#endif
      if (! match_args) {
        PyErr_Format(PyExc_TypeError,
                     "%R has no __match_args__ but %zd positional field "
                     "pattern(s) were supplied",
                     cls, num_positional);
        return -1;
      }
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


// The end.
