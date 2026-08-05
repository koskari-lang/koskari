/* Included by matcher.c — subject */
/* === subject adapters === */


static Py_ssize_t pyseq_length(const KosMatchSubject *s) {
  return s->cached_length;
}


static PyObject *pyseq_get(KosMatchSubject *s, Py_ssize_t i) {
  return PyTuple_GET_ITEM(s->backing, i);
}


static PyObject *pyseq_tail(KosMatchSubject *s, Py_ssize_t start) {
  return PyTuple_GetSlice(s->backing, start, s->cached_length);
}


static PyObject *pyseq_slice(
    KosMatchSubject *s,
    Py_ssize_t start,
    Py_ssize_t end) {

  return PyTuple_GetSlice(s->backing, start, end);
}


static void pyseq_release(KosMatchSubject *s) {
  Py_CLEAR(s->backing);
  s->cached_length = 0;
}


static const KosMatchSubjectVTable pyseq_vtable = {
  .length = (Py_ssize_t (*)(KosMatchSubject *)) pyseq_length,
  .get = pyseq_get,
  .tail = pyseq_tail,
  .slice = pyseq_slice,
  .release = pyseq_release,
};


int KosMatchSubject_FromPySequence(PyObject *seq, KosMatchSubject *out) {
  if (! seq || ! out) {
    PyErr_SetString(PyExc_ValueError,
                    "FromPySequence requires seq and out");
    return -1;
  }

  PyObject *fast = PySequence_Tuple(seq);
  if (! fast)
    return -1;

  out->vt = &pyseq_vtable;
  out->backing = fast;
  out->kp_cars = NULL;
  out->kp_tails = NULL;
  out->cached_length = PyTuple_GET_SIZE(fast);
  return 0;
}


static Py_ssize_t single_length(const KosMatchSubject *s) {
  (void) s;
  return 1;
}


static PyObject *single_get(KosMatchSubject *s, Py_ssize_t i) {
  (void) i;
  return s->backing;
}


static PyObject *single_slice(
    KosMatchSubject *s,
    Py_ssize_t start,
    Py_ssize_t end) {

  if (start <= 0 && end >= 1)
    return PyTuple_Pack(1, s->backing);
  return PyTuple_New(0);
}


static PyObject *single_tail(KosMatchSubject *s, Py_ssize_t start) {
  return single_slice(s, start, 1);
}


static void single_release(KosMatchSubject *s) {
  Py_CLEAR(s->backing);
  s->cached_length = 0;
}


static const KosMatchSubjectVTable single_vtable = {
  .length = (Py_ssize_t (*)(KosMatchSubject *)) single_length,
  .get = single_get,
  .tail = single_tail,
  .slice = single_slice,
  .release = single_release,
};


void KosMatchSubject_FromSingle(PyObject *value, KosMatchSubject *out) {
  out->vt = &single_vtable;
  Py_INCREF(value);
  out->backing = value;
  out->kp_cars = NULL;
  out->kp_tails = NULL;
  out->cached_length = 1;
}


static Py_ssize_t kospair_length(const KosMatchSubject *s) {
  return s->cached_length;
}


static PyObject *kospair_get(KosMatchSubject *s, Py_ssize_t i) {
  return matcher_peel_datum(s->kp_cars[i]);
}


static PyObject *kospair_tail(KosMatchSubject *s, Py_ssize_t start) {
  PyObject *t = s->kp_tails[start];
  Py_INCREF(t);
  return t;
}


static PyObject *kospair_slice(
    KosMatchSubject *s,
    Py_ssize_t start,
    Py_ssize_t end) {

  /* Build a fresh tuple of car values for the half-open range
     [start, end). Slice semantics are tuple-shaped because the engine
     uses slices only for multi-element CAPTURE values, not for REST. */
  Py_ssize_t n = end - start;
  if (n < 0)
    n = 0;
  PyObject *t = PyTuple_New(n);
  if (! t)
    return NULL;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *v = matcher_peel_datum(s->kp_cars[start + i]);
    Py_INCREF(v);
    PyTuple_SET_ITEM(t, i, v);
  }
  return t;
}


static void kospair_release(KosMatchSubject *s) {
  Py_CLEAR(s->backing);
  PyMem_Free(s->kp_cars);
  PyMem_Free(s->kp_tails);
  s->kp_cars = NULL;
  s->kp_tails = NULL;
  s->cached_length = 0;
}


static const KosMatchSubjectVTable kospair_vtable = {
  .length = (Py_ssize_t (*)(KosMatchSubject *)) kospair_length,
  .get = kospair_get,
  .tail = kospair_tail,
  .slice = kospair_slice,
  .release = kospair_release,
};


int KosMatchSubject_FromKosPair(
    PyObject *pair_or_nil,
    KosMatchSubject *out) {

  if (! pair_or_nil || ! out) {
    PyErr_SetString(PyExc_ValueError,
                    "FromKosPair requires pair_or_nil and out");
    return -1;
  }

  /* First pass: count length */
  Py_ssize_t n = 0;
  PyObject *cursor = pair_or_nil;
  while (KosPair_IsCell(cursor)) {
    n++;
    cursor = KosPair_CDR(cursor);
  }
  if (! KosNilP(cursor)) {
    PyErr_SetString(PyExc_TypeError,
                    "FromKosPair requires a proper list (nil-terminated)");
    return -1;
  }

  PyObject **cars = NULL;
  PyObject **tails = NULL;
  if (n > 0) {
    cars = PyMem_Malloc(n * sizeof(PyObject *));
    if (! cars) {
      PyErr_NoMemory();
      return -1;
    }
  }
  /* tails has n+1 entries: tails[i] = pair-at-position-i,
     tails[n] = KosNil (the final nil cell). */
  tails = PyMem_Malloc((n + 1) * sizeof(PyObject *));
  if (! tails) {
    PyMem_Free(cars);
    PyErr_NoMemory();
    return -1;
  }

  cursor = pair_or_nil;
  for (Py_ssize_t i = 0; i < n; i++) {
    tails[i] = cursor;
    cars[i] = KosPair_CAR(cursor);
    cursor = KosPair_CDR(cursor);
  }
  tails[n] = cursor;  /* KosNil */

  out->vt = &kospair_vtable;
  Py_INCREF(pair_or_nil);
  out->backing = pair_or_nil;
  out->kp_cars = cars;
  out->kp_tails = tails;
  out->cached_length = n;
  return 0;
}


/**
   Build a subject from an arbitrary Python value: KosPair / nil takes
   the KosPair adapter; anything else falls through to PySequence.
   Returns 0 on success, -1 on error. Out subject must be released
   with KosMatchSubject_Release.
*/
static int subject_from_any(PyObject *value, KosMatchSubject *out) {
  /* Reader output often wraps pair-tree cells in Datum. Iterating a pair as
     a Python sequence yields (car, cdr), which breaks FORM on a single list
     cell such as ((t 1)): the matcher would see two spine elements and
     fail to consume the trailing nil. Peel Datum layers so KosPair lists
     use the proper-list spine adapter. */
  value = matcher_peel_datum(value);
  if (KosPair_Check(value) || KosNilP(value))
    return KosMatchSubject_FromKosPair(value, out);
  return KosMatchSubject_FromPySequence(value, out);
}


void KosMatchSubject_Release(KosMatchSubject *s) {
  if (! s || ! s->vt)
    return;
  s->vt->release(s);
  s->vt = NULL;
}


// The end.
