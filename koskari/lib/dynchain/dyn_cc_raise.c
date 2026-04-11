/* Included by dynchain.c — dyn_cc_raise */
/* === KosDynCCRaise implementation === */


/**
   Sentinel holding exception for foreign apply; unwind sets PyErr. Callers:
   kos_build_cc_unwind_plan.
*/
static KosDynCCRaise *kos_dyn_cc_raise_new(KosDynNode *parent, PyObject *exc_type, PyObject *exc_val) {
  KosDynCCRaise *node = (KosDynCCRaise *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_CC_RAISE, parent,
                   KOS_DYN_CAP_NEEDS_UNWIND_ACTION);
  Py_INCREF(exc_type);
  KosDynCCRaise_EXC_TYPE(node) = exc_type;
  Py_INCREF(exc_val);
  KosDynCCRaise_EXC_VAL(node) = exc_val;
  return node;
}


/**
   DECREF exc refs; return node to freelist.
*/
static void kos_dyn_cc_raise_free(KosDynCCRaise *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynCCRaise_EXC_TYPE(node));
  KosDynCCRaise_EXC_TYPE(node) = NULL;
  Py_XDECREF(KosDynCCRaise_EXC_VAL(node));
  KosDynCCRaise_EXC_VAL(node) = NULL;
  kos_dynnode_return_to_freelist(node);
}


/**
   Transfer EXTENT_ACTION target: set PyErr from stored exception; returns -1.
   Callers: kos_dyn_unwind for KOS_DYN_CC_RAISE.
*/
static int cc_raise_unwind(KosContext *ctx, KosDynNode *node,
                           KosEvent *out_ev) {
  (void) ctx;
  (void) out_ev;
  PyObject *exc_type = KosDynCCRaise_EXC_TYPE(node);
  PyObject *exc_val = KosDynCCRaise_EXC_VAL(node);
  if (exc_type && exc_val) {
    Py_INCREF(exc_val);
    PyErr_SetObject(exc_type, exc_val);
    Py_DECREF(exc_val);
  } else if (exc_val) {
    PyObject *typ = (PyObject *) Py_TYPE(exc_val);
    Py_INCREF(exc_val);
    PyErr_SetObject(typ, exc_val);
    Py_DECREF(exc_val);
  } else {
    PyErr_SetString(PyExc_RuntimeError,
                    "cc_raise_unwind: missing exception value");
  }
  return -1;
}
