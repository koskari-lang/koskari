/* Included by matcher.c — datum */
/* === Datum === */

static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding,
    int from_datum);

typedef struct {
  PyObject_HEAD
  PyObject *current;   /* KosPair-or-nil; tail pointer */
  PyObject *fallback;  /* fallback position */
  PyObject *binding;   /* capture slot name or Py_None */
  int from_datum;      /* inherited from parent Datum */
} KosDatumIter;


static void datum_iter_dealloc(PyObject *self) {
  KosDatumIter *it = (KosDatumIter *) self;
  Py_CLEAR(it->current);
  Py_CLEAR(it->fallback);
  Py_CLEAR(it->binding);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *datum_iter_iternext(PyObject *self) {
  KosDatumIter *it = (KosDatumIter *) self;

  if (! it->current || KosNilP(it->current)) {
    Py_CLEAR(it->current);
    return NULL;
  }

  if (! KosPair_Check(it->current)) {
    /* Improper tail; terminate */
    Py_CLEAR(it->current);
    return NULL;
  }

  PyObject *cell = it->current;
  PyObject *elem = KosPair_CAR(cell);
  PyObject *pos = KosPair_POSITION(cell);
  if (! pos || pos == Py_None) {
    pos = it->fallback;
  }
  if (! pos)
    pos = Py_None;

  it->current = KosPair_CDR(cell);
  Py_INCREF(it->current);
  Py_DECREF(cell);

  Py_INCREF(elem);
  Py_INCREF(pos);
  Py_INCREF(it->binding);
  PyObject *out = kos_datum_new(
      elem, pos, it->binding, it->from_datum);
  Py_DECREF(elem);
  Py_DECREF(pos);
  Py_DECREF(it->binding);
  return out;
}


static PyTypeObject KosDatumIterType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.DatumIter",
  .tp_basicsize = sizeof(KosDatumIter),
  .tp_dealloc = datum_iter_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = datum_iter_iternext,
};


static void datum_dealloc(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  Py_CLEAR(d->datum);
  Py_CLEAR(d->position);
  Py_CLEAR(d->binding);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *datum_repr(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  return PyUnicode_FromFormat(
      "<Datum datum=%R position=%R binding=%R>",
      d->datum, d->position, d->binding);
}


static PyObject *datum_get_datum(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->datum);
  return d->datum;
}


static PyObject *datum_get_position(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->position);
  return d->position;
}


static PyObject *datum_get_binding(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->binding);
  return d->binding;
}


static PyObject *datum_get_from_datum(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyBool_FromLong(d->from_datum);
}


static PyObject *datum_get_car(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetAttrString(d->datum, "car");
}


static PyObject *datum_get_cdr(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetAttrString(d->datum, "cdr");
}


static PyObject *datum_iter(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  if (! KosPair_Check(d->datum)) {
    return PyErr_Format(
        PyExc_TypeError,
        "cannot iterate Datum with non-pair datum: %R",
        d->datum);
  }

  KosDatumIter *it = PyObject_New(KosDatumIter, &KosDatumIterType);
  if (! it)
    return NULL;

  Py_INCREF(d->datum);
  Py_INCREF(d->position);
  Py_INCREF(d->binding);
  it->current = d->datum;
  it->fallback = d->position;
  it->binding = d->binding;
  it->from_datum = d->from_datum;
  return (PyObject *) it;
}


static PyObject *datum_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  PyObject *datum;
  PyObject *position = Py_None;
  PyObject *binding = Py_None;
  int from_datum = 0;
  static char *kwlist[] = {
    "datum", "position", "binding", "from_datum", NULL};

  (void) type;
  if (! PyArg_ParseTupleAndKeywords(
          args, kwds, "O|OOp:Datum", kwlist,
          &datum, &position, &binding, &from_datum))
    return NULL;

  return kos_datum_new(datum, position, binding, from_datum);
}


static PyGetSetDef datum_getset[] = {
  { "datum", datum_get_datum, NULL,
    "Matched value", NULL },
  { "position", datum_get_position, NULL,
    "Source position from enclosing pair cell, or None", NULL },
  { "binding", datum_get_binding, NULL,
    "Capture slot name (str) or None", NULL },
  { "from_datum", datum_get_from_datum, NULL,
    "True when matched inside a substitution Datum box", NULL },
  { "car", datum_get_car, NULL,
    "Underlying datum car (delegates to pair)", NULL },
  { "cdr", datum_get_cdr, NULL,
    "Underlying datum cdr (delegates to pair)", NULL },
  { NULL }
};


PyTypeObject KosDatumType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Datum",
  .tp_basicsize = sizeof(KosDatum),
  .tp_dealloc = datum_dealloc,
  .tp_repr = datum_repr,
  .tp_iter = datum_iter,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Matched value with optional KosPair source position and "
            "capture binding name",
  .tp_getset = datum_getset,
  .tp_new = datum_tp_new,
};


static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding,
    int from_datum) {

  KosDatum *d = PyObject_New(KosDatum, &KosDatumType);
  if (! d)
    return NULL;
  if (! binding)
    binding = Py_None;
  Py_INCREF(datum);
  Py_INCREF(position);
  Py_INCREF(binding);
  d->datum = datum;
  d->position = position;
  d->binding = binding;
  d->from_datum = from_datum ? 1 : 0;
  return (PyObject *) d;
}


/**
   Set KosDatum.binding to slot name (in-place). Recurse into tuples of
   Datum (slice captures). ``name`` is the capture key (borrowed ok;
   we incref when storing).
*/
static void kos_datum_annotate_binding(PyObject *value, PyObject *name) {
  Py_ssize_t i;
  Py_ssize_t n;

  if (! value || ! name)
    return;
  if (KosDatum_Check(value)) {
    KosDatum *d = (KosDatum *) value;
    Py_INCREF(name);
    PyObject *old_b = d->binding;
    d->binding = name;
    Py_DECREF(old_b);
    return;
  }
  if (PyTuple_Check(value)) {
    n = PyTuple_GET_SIZE(value);
    for (i = 0; i < n; i++)
      kos_datum_annotate_binding(PyTuple_GET_ITEM(value, i), name);
  }
}


/* === DatumFrame === */


static int datum_frame_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_VISIT(f->source);
  Py_VISIT(f->position);
  if (PyDict_Type.tp_traverse)
    return PyDict_Type.tp_traverse(self, visit, arg);
  return 0;
}


static int datum_frame_clear(PyObject *self) {

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_CLEAR(f->source);
  Py_CLEAR(f->position);
  if (PyDict_Type.tp_clear)
    return PyDict_Type.tp_clear(self);
  return 0;
}


static void datum_frame_dealloc(PyObject *self) {

  datum_frame_clear(self);
  PyDict_Type.tp_dealloc(self);
}


static PyObject *datum_frame_repr(PyObject *self) {

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_ssize_t nslots = PyDict_Size(self);
  return PyUnicode_FromFormat(
      "<DatumFrame source=%R position=%R captures=%zd>",
      f->source, f->position, nslots);
}


static PyObject *datum_frame_get_source(PyObject *self, void *closure) {
  (void) closure;
  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(f->source);
  return f->source;
}


static PyObject *datum_frame_get_position(PyObject *self, void *closure) {
  (void) closure;
  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(f->position);
  return f->position;
}


static PyObject *datum_frame_get_from_datum(PyObject *self, void *closure) {
  (void) closure;
  KosDatumFrame *f = (KosDatumFrame *) self;
  return PyBool_FromLong(f->from_datum);
}


static PyGetSetDef datum_frame_getset[] = {
  { "source", datum_frame_get_source, NULL,
    "Matched source represented by this capture frame", NULL },
  { "position", datum_frame_get_position, NULL,
    "Source position from the frame start, or None", NULL },
  { "from_datum", datum_frame_get_from_datum, NULL,
    "True when the matched spine cell was a substitution Datum", NULL },
  { NULL }
};


PyTypeObject KosDatumFrameType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.DatumFrame",
  .tp_basicsize = sizeof(KosDatumFrame),
  .tp_dealloc = datum_frame_dealloc,
  .tp_repr = datum_frame_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Capture dict with source provenance",
  .tp_traverse = datum_frame_traverse,
  .tp_clear = datum_frame_clear,
  .tp_getset = datum_frame_getset,
  .tp_base = &PyDict_Type,
};


static PyObject *kos_datum_frame_new(void) {

  PyObject *self = PyObject_CallNoArgs((PyObject *) &KosDatumFrameType);
  if (! self)
    return NULL;

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(Py_None);
  Py_INCREF(Py_None);
  f->source = Py_None;
  f->position = Py_None;
  f->from_datum = 0;
  return self;
}


/* === MatchResult === */


static void match_result_dealloc(PyObject *self) {
  KosMatchResult *r = (KosMatchResult *) self;
  Py_CLEAR(r->captures);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *match_result_repr(PyObject *self) {
  KosMatchResult *r = (KosMatchResult *) self;
  Py_ssize_t nslots = r->captures ? PyDict_Size(r->captures) : 0;
  return PyUnicode_FromFormat(
      "<MatchResult matched=%s captures=%zd>",
      r->matched ? "True" : "False", nslots);
}


static int match_result_bool(PyObject *self) {
  return ((KosMatchResult *) self)->matched ? 1 : 0;
}


static PyNumberMethods match_result_as_number = {
  .nb_bool = match_result_bool,
};


static PyObject *match_result_get_matched(PyObject *self, void *closure) {
  (void) closure;
  return PyBool_FromLong(((KosMatchResult *) self)->matched);
}


static PyObject *match_result_get_captures(PyObject *self, void *closure) {
  (void) closure;
  PyObject *d = ((KosMatchResult *) self)->captures;
  Py_INCREF(d);
  return d;
}


static PyGetSetDef match_result_getset[] = {
  { "matched", match_result_get_matched, NULL,
    "True if the matcher matched the subject", NULL },
  { "captures", match_result_get_captures, NULL,
    "dict[str, value] of captured slot values", NULL },
  { NULL }
};


PyTypeObject KosMatchResultType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.MatchResult",
  .tp_basicsize = sizeof(KosMatchResult),
  .tp_dealloc = match_result_dealloc,
  .tp_repr = match_result_repr,
  .tp_as_number = &match_result_as_number,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Result of running a matcher against a subject",
  .tp_getset = match_result_getset,
};


static PyObject *make_match_result(
    int matched,
    PyObject *captures) {

  KosMatchResult *r = PyObject_New(KosMatchResult, &KosMatchResultType);
  if (! r)
    return NULL;
  Py_INCREF(captures);
  r->captures = captures;
  r->matched = matched;
  return (PyObject *) r;
}


/**
   Borrow through outer ``Datum`` layers for KosPair spine reads. The
   underlying pair tree and hygiene boxes are unchanged; only matching
   sees the inner value.
*/
static PyObject *matcher_peel_datum(PyObject *obj) {
  while (obj && KosDatum_Check(obj)) {
    KosDatum *const d = (KosDatum *) obj;
    obj = d->datum;
  }
  return obj;
}


/**
   Cached collections.abc.Sequence, resolved lazily on the first
   non-fast-path FORM descent and then immortal — the same shape as
   get_abc_mapping, for the same reason: the FORM miss path runs for
   every non-sequence element that flows past a sequence arm. Returns a
   borrowed reference, or NULL with an exception set.
*/
static PyObject *get_abc_sequence(void) {
  static PyObject *abc_sequence_type = NULL;

  if (! abc_sequence_type) {
    PyObject *abc = PyImport_ImportModule("collections.abc");
    if (! abc)
      return NULL;
    abc_sequence_type = PyObject_GetAttrString(abc, "Sequence");
    Py_DECREF(abc);
  }
  return abc_sequence_type;
}


/**
   Invoke a callable extern (predicate, APP extractor) on one argument
   through the demand path. Matching never enters the evaluator, so the
   call is driven synchronously: koskari callables run on a demand eval
   drive, plain Python awaitable results are eagerly stepped, and a
   genuine async suspension raises KoskariSyncViolation. Returns a new
   reference, or NULL with an exception set.
*/
static PyObject *matcher_call_extern(PyObject *fn, PyObject *arg) {
  PyObject *args = PyTuple_Pack(1, arg);
  if (! args)
    return NULL;
  PyObject *res = Koskari_DemandSync(fn, args, NULL);
  Py_DECREF(args);
  return res;
}

// The end.
