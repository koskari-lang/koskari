/* Included by dynchain.c — dyn_param */
/* === KosDynParam implementation === */


/* Sentinel for "param was unset" in saved dict; value must not be a valid param value.
   Initialized in koskari_dynchain_init, cleared in koskari_dynchain_clear. */
static PyObject *_param_sentinel_unset = NULL;


/**
   Build PARAM node from owned bindings dict (caller transfers ownership).
*/
static KosDynParam *kos_dynparam_new_from_bindings(
    KosDynNode *parent,
    PyObject *bindings) {

  KosDynParam *node = (KosDynParam *) kos_dynnode_alloc();
  if (! node) {
    Py_DECREF(bindings);
    return NULL;
  }

  kos_dynnode_init(
    (KosDynNode *) node,
    KOS_DYN_PARAM,
    parent,
    KOS_DYN_CAP_NEEDS_UNWIND_ACTION | KOS_DYN_CAP_NEEDS_INSTALL_WIND);

  KosDynParam_BINDINGS(node) = bindings;
  KosDynParam_SAVED(node) = NULL;

  return node;
}


/**
   Build PARAM from parallel param ref / value arrays (creates dict).
*/
static KosDynParam *kos_dynparam_new(
    KosDynNode *parent,
    uint32_t count,
    PyObject **params,
    PyObject **values) {

  if (count == 0) {
    PyErr_SetString(PyExc_ValueError, "KosDynParam requires at least one binding");
    return NULL;
  }

  PyObject *bindings = PyDict_New();
  if (! bindings)
    return NULL;

  for (uint32_t i = 0; i < count; i++) {
    PyObject *key = KosParamRef_KEY(params[i]);
    if (PyDict_SetItem(bindings, key, values[i]) < 0) {
      Py_DECREF(bindings);
      return NULL;
    }
  }

  return kos_dynparam_new_from_bindings(parent, bindings);
}


/**
   PyDict_Copy bindings for INSTALL/PUSH segments.
*/
static KosDynParam *kos_dynparam_clone(KosDynParam *node) {

  if (! node)
    return NULL;

  PyObject *bindings = KosDynParam_BINDINGS(node);
  PyObject *copy = PyDict_Copy(bindings);
  if (! copy)
    return NULL;

  KosDynParam *clone = (KosDynParam *) kos_dynnode_alloc();
  if (! clone) {
    Py_DECREF(copy);
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_PARAM, NULL,
                   KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynParam_BINDINGS(clone) = copy;
  KosDynParam_SAVED(clone) = NULL;

  return clone;
}


/**
   DECREF bindings and saved; return to freelist.
*/
static void kos_dynparam_free(KosDynParam *node) {

  if (! node)
    return;

  Py_XDECREF(KosDynParam_BINDINGS(node));
  KosDynParam_BINDINGS(node) = NULL;
  Py_XDECREF(KosDynParam_SAVED(node));
  KosDynParam_SAVED(node) = NULL;

  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


/**
   Apply bindings to KosContext domain; stash prior values in SAVED dict.
*/
static int kos_dynparam_enter(KosContext *ctx, KosDynParam *node) {

  PyObject *domain = KosContext_DOMAIN(ctx);
  if (! domain) {
    PyErr_SetString(PyExc_RuntimeError,
                    "param enter requires active domain");
    return -1;
  }

  PyObject *bindings = KosDynParam_BINDINGS(node);
  PyObject *saved = PyDict_New();
  if (! saved)
    return -1;

  PyObject *sentinel = _param_sentinel_unset;
  if (! sentinel) {
    Py_DECREF(saved);
    return -1;
  }

  PyObject *key;
  PyObject *value;
  Py_ssize_t pos = 0;

  while (PyDict_Next(bindings, &pos, &key, &value)) {
    PyObject *cur = kos_domain_get_param(domain, key);
    PyObject *to_save = cur ? cur : sentinel;
    if (PyDict_SetItem(saved, key, to_save) < 0) {
      Py_XDECREF(cur);
      Py_DECREF(saved);
      return -1;
    }
    if (cur)
      Py_DECREF(cur);  /* SetItem holds its own ref */
    if (kos_domain_set_param(domain, key, value) < 0) {
      Py_DECREF(saved);
      return -1;
    }
  }

  KosDynParam_SAVED(node) = saved;
  return 0;
}


/**
   Restore domain from SAVED (unwind / transfer).
*/
static void kos_dynparam_restore(KosContext *ctx, KosDynParam *node) {

  PyObject *domain = KosContext_DOMAIN(ctx);
  if (! domain)
    return;

  PyObject *saved = KosDynParam_SAVED(node);
  if (! saved)
    return;

  PyObject *sentinel = _param_sentinel_unset;
  PyObject *key;
  PyObject *old_val;
  Py_ssize_t pos = 0;

  while (PyDict_Next(saved, &pos, &key, &old_val)) {
    if (old_val == sentinel)
      kos_domain_clear_param(domain, key);
    else
      kos_domain_set_param(domain, key, old_val);
  }
}


/**
   INSTALL wind: enter bindings. Callers: kos_dyn_wind, install wind span.
*/
int kos_dyn_param_wind(KosEvalState *es, KosDynParam *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  return kos_dynparam_enter(ctx, node);
}


/**
   Restore outer param values on extent unwind. Callers: kos_dyn_unwind.
*/
void kos_dyn_param_unwind(KosEvalState *es, KosDynParam *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] param unwind node=%p", (void *) node);
  kos_dynparam_restore(ctx, node);
}


/**
   Push PARAM from dict: enter then link as DYN_TOP. Callers: opcode PUSH_PARAM.
*/
KosDynParam *kos_dyn_push_param_dict(KosEvalState *es, PyObject *bindings) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! bindings) {
    PyErr_SetString(PyExc_ValueError, "kos_dyn_push_param_dict: bindings is NULL");
    return NULL;
  }
  if (! PyDict_CheckExact(bindings)) {
    PyErr_Format(PyExc_TypeError, "kos_dyn_push_param_dict expected dict, got %R",
                 bindings);
    return NULL;
  }
  if (PyDict_Size(bindings) == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "KosDynParam requires at least one binding");
    return NULL;
  }

  KosDynParam *node = kos_dynparam_new_from_bindings(
      KosContext_DYN_TOP(ctx), bindings);
  if (! node)
    return NULL;

  if (kos_dynparam_enter(ctx, node) < 0) {
    kos_dynparam_free(node);
    return NULL;
  }

  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynparam_restore(ctx, node);
    kos_dynparam_free(node);
    return NULL;
  }
  return node;
}


/**
   Push PARAM from parallel arrays (vector call sites).
*/
KosDynParam *kos_dyn_push_param(
    KosEvalState *es,
    uint32_t count,
    PyObject **params,
    PyObject **values) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynParam *node = kos_dynparam_new(KosContext_DYN_TOP(ctx),
                                       count, params, values);
  if (! node)
    return NULL;

  if (kos_dynparam_enter(ctx, node) < 0) {
    kos_dynparam_free(node);
    return NULL;
  }

  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynparam_restore(ctx, node);
    kos_dynparam_free(node);
    return NULL;
  }
  return node;
}


/**
   Restore and unlink PARAM when top. Callers: POP_PARAM, transfer pop.
*/
void kos_dyn_pop_param(KosEvalState *es, KosDynParam *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    KosEvalState_SyncFromContext(es);
    return;
  }

  kos_dynparam_restore(ctx, node);
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynparam_free(node);
  }
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_PARAM: visit the parameter bindings dict and
   the saved bindings from outer scopes.
*/
static int dynnode_traverse_param(KosDynNode *node, visitproc visit, void *arg) {
  KosDynParam *param_node = (KosDynParam *) node;
  Py_VISIT(KosDynParam_BINDINGS(param_node));
  Py_VISIT(KosDynParam_SAVED(param_node));
  return 0;
}
