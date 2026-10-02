/* Included by matcher.c — datum */
/* === Datum === */

static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding);

typedef struct {
  PyObject_HEAD
  PyObject *current;   /* KosPair-or-nil; tail pointer */
  PyObject *fallback;  /* fallback position */
  PyObject *binding;   /* capture slot name or Py_None */
} KosDatumIter;


static void datum_iter_dealloc(PyObject *self) {
  KosDatumIter *it = (KosDatumIter *) self;
  PyObject_GC_UnTrack(self);
  Py_CLEAR(it->current);
  Py_CLEAR(it->fallback);
  Py_CLEAR(it->binding);
  Py_TYPE(self)->tp_free(self);
}


static int datum_iter_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosDatumIter *it = (KosDatumIter *) self;
  Py_VISIT(it->current);
  Py_VISIT(it->fallback);
  Py_VISIT(it->binding);
  return 0;
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

  /* the cell owns elem and pos; hold them before the cell can go */
  Py_INCREF(elem);
  Py_INCREF(pos);

  it->current = KosPair_CDR(cell);
  Py_INCREF(it->current);
  Py_DECREF(cell);

  Py_INCREF(it->binding);
  PyObject *out = kos_datum_new(elem, pos, it->binding);
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
  .tp_traverse = datum_iter_traverse,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = datum_iter_iternext,
};


static void datum_dealloc(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  PyObject_GC_UnTrack(self);
  Py_CLEAR(d->datum);
  Py_CLEAR(d->position);
  Py_CLEAR(d->binding);
  Py_TYPE(self)->tp_free(self);
}


static int datum_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosDatum *d = (KosDatum *) self;
  Py_VISIT(d->datum);
  Py_VISIT(d->position);
  Py_VISIT(d->binding);
  return 0;
}


static PyObject *datum_repr(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  return PyUnicode_FromFormat(
      "<koskari datum %R position=%R binding=%R>",
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

  KosDatumIter *it = PyObject_GC_New(KosDatumIter, &KosDatumIterType);
  if (! it)
    return NULL;

  Py_INCREF(d->datum);
  Py_INCREF(d->position);
  Py_INCREF(d->binding);
  it->current = d->datum;
  it->fallback = d->position;
  it->binding = d->binding;
  PyObject_GC_Track((PyObject *) it);
  return (PyObject *) it;
}


static PyObject *datum_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  PyObject *datum;
  PyObject *position = Py_None;
  PyObject *binding = Py_None;
  static char *kwlist[] = {"datum", "position", "binding", NULL};

  (void) type;
  if (! PyArg_ParseTupleAndKeywords(
          args, kwds, "O|OO:Datum", kwlist,
          &datum, &position, &binding))
    return NULL;

  return kos_datum_new(datum, position, binding);
}


static PyGetSetDef datum_getset[] = {
  { "datum", datum_get_datum, NULL,
    "Matched value", NULL },
  { "position", datum_get_position, NULL,
    "Source position from enclosing pair cell, or None", NULL },
  { "binding", datum_get_binding, NULL,
    "Capture slot name (str) or None", NULL },
  { "car", datum_get_car, NULL,
    "Underlying datum car (delegates to pair)", NULL },
  { "cdr", datum_get_cdr, NULL,
    "Underlying datum cdr (delegates to pair)", NULL },
  { NULL }
};


PyTypeObject KosDatumType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.datum",
  .tp_basicsize = sizeof(KosDatum),
  .tp_dealloc = datum_dealloc,
  .tp_repr = datum_repr,
  .tp_iter = datum_iter,
  .tp_traverse = datum_traverse,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Matched value with optional KosPair source position and "
            "capture binding name",
  .tp_getset = datum_getset,
  .tp_new = datum_tp_new,
};


static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding) {

  KosDatum *d = PyObject_GC_New(KosDatum, &KosDatumType);
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
  PyObject_GC_Track((PyObject *) d);
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


/* Every DatumFrame starts with source and position set to None, from
   whichever side constructs it: the getters and the frame writers in
   matcher.c rely on both fields being real objects. */
static PyObject *datum_frame_new(PyTypeObject *type, PyObject *args,
                                 PyObject *kwds) {
  PyObject *self = PyDict_Type.tp_new(type, args, kwds);
  if (! self)
    return NULL;

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(Py_None);
  f->source = Py_None;
  Py_INCREF(Py_None);
  f->position = Py_None;
  return self;
}


static PyGetSetDef datum_frame_getset[] = {
  { "source", datum_frame_get_source, NULL,
    "Matched source represented by this capture frame", NULL },
  { "position", datum_frame_get_position, NULL,
    "Source position from the frame start, or None", NULL },
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
  .tp_new = datum_frame_new,
  .tp_traverse = datum_frame_traverse,
  .tp_clear = datum_frame_clear,
  .tp_getset = datum_frame_getset,
  .tp_base = &PyDict_Type,
};


static PyObject *kos_datum_frame_new(void) {
  /* tp_new fills source and position */
  return PyObject_CallNoArgs((PyObject *) &KosDatumFrameType);
}


/* === MatchResult === */


static void match_result_dealloc(PyObject *self) {
  KosMatchResult *r = (KosMatchResult *) self;
  PyObject_GC_UnTrack(self);
  Py_CLEAR(r->captures);
  Py_TYPE(self)->tp_free(self);
}


static int match_result_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  Py_VISIT(((KosMatchResult *) self)->captures);
  return 0;
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
  .tp_traverse = match_result_traverse,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Result of running a matcher against a subject",
  .tp_getset = match_result_getset,
};


static PyObject *make_match_result(
    int matched,
    PyObject *captures) {

  KosMatchResult *r = PyObject_GC_New(KosMatchResult, &KosMatchResultType);
  if (! r)
    return NULL;
  Py_INCREF(captures);
  r->captures = captures;
  r->matched = matched;
  PyObject_GC_Track((PyObject *) r);
  return (PyObject *) r;
}


/**
   Borrow through outer ``Datum`` layers for KosPair spine reads. The
   underlying pair tree and the boxes are unchanged; only matching
   sees the inner value. A gensym is a symbol and is never peeled: it
   is the identity a capture hands back to the compiler.
*/
static PyObject *matcher_peel_datum(PyObject *obj) {
  while (obj && KosDatum_Check(obj))
    obj = ((KosDatum *) obj)->datum;
  return obj;
}


/**
   Invoke a callable extern (predicate, APP extractor) on one argument
   on the demand path: a Koskari callable runs a nested evaluator, a
   plain Python awaitable result is eagerly stepped, and a genuine
   async suspension raises KoskariSyncViolation. This is a flattening
   gap, not the design: the match walk holds its position on the C
   stack, so it cannot yet suspend on the call and resume with the
   delivered value (design/FLATTENING.md, design/interpreter/
   resumable-reads.md section 8). Returns a new reference, or NULL
   with an exception set.
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
