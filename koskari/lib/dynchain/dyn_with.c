/* Included by dynchain.c — dyn_with */
/* === KosDynWith (single-shot context manager) === */


/**
   Single-shot WITH: catch cap, resume_pc after body. Callers: kos_dyn_push_with.
*/
static KosDynWith *kos_dynwith_new(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, parent,
                   KOS_DYN_CAP_CATCH | KOS_DYN_CAP_NEEDS_UNWIND_ACTION);
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


/**
   Multi-shot dyn-with: install_wind + unwind caps for enter/exit thunks.
*/
static KosDynWith *kos_dyndynwith_new(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_DYN_WITH, parent,
                   KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION | KOS_DYN_CAP_NEEDS_UNWIND_ACTION
                   | KOS_DYN_CAP_NEEDS_INSTALL_WIND
                  );
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


/**
   Clone mgr/enter/exit/name for continuation segment (not in pending await).
*/
KosDynWith *kos_dynwith_clone(KosDynWith *src) {
  if (! src)
    return NULL;
  /* Invariant: do not clone a node in PENDING_EXIT_AWAIT or PENDING_ENTER_AWAIT */
  if (KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT
      || KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return NULL;

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, NULL,
                   KosDynNode_CAPS(src));
  KosDynNode_FLAGS(node) = KosDynNode_FLAGS(src) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(node) = KosDynNode_NODE_ID(src);
  KosDynWith_MGR(node) = KosDynWith_MGR(src);
  Py_XINCREF(KosDynWith_MGR(node));
  KosDynWith_ENTER(node) = KosDynWith_ENTER(src);
  Py_XINCREF(KosDynWith_ENTER(node));
  KosDynWith_EXIT(node) = KosDynWith_EXIT(src);
  Py_XINCREF(KosDynWith_EXIT(node));
  KosDynWith_ENTERED(node) = KosDynWith_ENTERED(src);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


/**
   Clone DYN_WITH payload like kos_dynwith_clone.
*/
KosDynWith *kos_dyndynwith_clone(KosDynWith *src) {
  if (! src)
    return NULL;
  /* Invariant: do not clone a node in PENDING_EXIT_AWAIT or PENDING_ENTER_AWAIT */
  if (KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT
      || KosDynWith_PHASE(src) == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return NULL;

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_DYN_WITH, NULL,
                   KosDynNode_CAPS(src));
  KosDynNode_FLAGS(node) = KosDynNode_FLAGS(src) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(node) = KosDynNode_NODE_ID(src);
  KosDynWith_MGR(node) = KosDynWith_MGR(src);
  Py_XINCREF(KosDynWith_MGR(node));
  KosDynWith_ENTER(node) = KosDynWith_ENTER(src);
  Py_XINCREF(KosDynWith_ENTER(node));
  KosDynWith_EXIT(node) = KosDynWith_EXIT(src);
  Py_XINCREF(KosDynWith_EXIT(node));
  KosDynWith_ENTERED(node) = KosDynWith_ENTERED(src);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


/**
   DECREF mgr/enter/exit/name and pending exc triple; return to freelist.
*/
static void kos_dynwith_free(KosDynWith *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynWith_MGR(node));
  KosDynWith_MGR(node) = NULL;
  Py_XDECREF(KosDynWith_ENTER(node));
  KosDynWith_ENTER(node) = NULL;
  Py_XDECREF(KosDynWith_EXIT(node));
  KosDynWith_EXIT(node) = NULL;
  Py_XDECREF(KosDynWith_NAME(node));
  KosDynWith_NAME(node) = NULL;
  if (KosDynWith_PHASE(node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    Py_XDECREF(KosDynWith_EXC_TYP(node));
    KosDynWith_EXC_TYP(node) = NULL;
    Py_XDECREF(KosDynWith_EXC_VAL(node));
    KosDynWith_EXC_VAL(node) = NULL;
    Py_XDECREF(KosDynWith_EXC_TB(node));
    KosDynWith_EXC_TB(node) = NULL;
  }

  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


/**
   Push WITH as DYN_TOP with ENTERED=1 after sync __enter__. Callers: PUSH_WITH.
*/
KosDynWith *kos_dyn_push_with(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 1;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


/**
   Push DYN_WITH as DYN_TOP (multi-shot). Callers: PUSH_DYN_WITH.
*/
KosDynWith *kos_dyn_push_dyn_with(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dyndynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 1;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


/**
   Push WITH before async __enter__ completes (PENDING_ENTER_AWAIT).
*/
KosDynWith *kos_dyn_push_with_provisional(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


/**
   Push DYN_WITH before async __enter__ completes.
*/
KosDynWith *kos_dyn_push_dyn_with_provisional(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  KosDynWith *node = kos_dyndynwith_new(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  return node;
}


/**
   Unlink WITH when top without running __exit__ (fast path only).
*/
void kos_dyn_pop_with(KosContext *ctx, KosDynWith *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynwith_free(node);
}


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)  KosDynFrame_STACK(CURRENT_FRAME(ctx))
#define CURRENT_PC(ctx)       KosDynFrame_PC(CURRENT_FRAME(ctx))


/**
   Bind name in frame namespace for `as` targets.
*/
static int frame_ns_store(KosContext *ctx, PyObject *name, PyObject *val) {
  KosDynFrame *frame = CURRENT_FRAME(ctx);
  if (! frame)
    return -1;
  PyObject *ns = KosDynFrame_NS(frame);
  if (KosNamespace_Check(ns))
    return KosNamespace_Store(ns, name, val);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Resume after awaitable __enter__: bind name, clear PENDING_ENTER_AWAIT.
*/
static int kos_dyn_with_on_enter_control_returned(KosContext *ctx, KosDynWith *with_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return 0;

  if (! KosContext_FRAME_TOP(ctx))
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/**
   Resume after awaitable __exit__: merge result with exc triple, maybe pop node.
*/
static int kos_dyn_with_on_control_returned(KosContext *ctx, KosDynWith *with_node,
                                            int do_pop_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
    return 0;

  if (! KosContext_FRAME_TOP(ctx))
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *typ = KosDynWith_EXC_TYP(with_node);
  PyObject *val = KosDynWith_EXC_VAL(with_node);
  PyObject *tb = KosDynWith_EXC_TB(with_node);
  KosDynWith_EXC_TYP(with_node) = NULL;
  KosDynWith_EXC_VAL(with_node) = NULL;
  KosDynWith_EXC_TB(with_node) = NULL;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;

  int had_exception = (typ != Py_None);

  if (had_exception && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_DECREF(result);
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
      return -1;
    if (KosContext_FRAME_TOP(ctx))
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
    if (do_pop_node) {
      kos_dyn_pop_with(ctx, with_node);
    }
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
      return -1;
    return 0;
  }

  if (had_exception) {
    Py_DECREF(result);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
      PyErr_Restore(typ, val, tb);
      return -1;
    }
    if (do_pop_node) {
      kos_dyn_pop_with(ctx, with_node);
    }
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  Py_DECREF(result);
  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  PyObject *body_val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! body_val)
    return -1;
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  if (KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
  if (do_pop_node) {
    kos_dyn_pop_with(ctx, with_node);
  }
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), body_val) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  Py_DECREF(body_val);
  return 0;
}


/**
   INSTALL wind: sync __enter__ for DYN_WITH during transfer. Callers: kos_dyn_wind.
*/
static int kos_dyn_dyn_with_wind(KosContext *ctx, KosDynWith *node, KosEvent *out_ev) {
  PyObject *enter_fn = KosDynWith_ENTER(node);
  int r = KosContext_CallNoArgs(ctx, out_ev, enter_fn, 0);
  if (r == 1)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (r < 0)
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *name = KosDynWith_NAME(node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(node) = 1;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/**
   Complete provisional WITH after __enter__ result is ready (non-await path).
*/
int finalize_provisional_with_enter(KosContext *ctx, PyObject *result,
                                    KosDynWith *with_node) {

  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}
