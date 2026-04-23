/* Included by dynchain.c — dyn_with */
/* === KosDynWith (context manager; multi/single via u.with.shot) === */


static void kos_dynwith_free(KosDynWith *node);


/**
   Allocate a WITH node: kind KOS_DYN_WITH, caps from shot (multi vs single).
*/
static KosDynWith *kos_dyn_with_allocate(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot) {

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();
  if (! node)
    return NULL;

  {
    uint32_t caps;
    if (shot == KOS_DYN_WITH_SHOT_MULTI)
      caps = KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION | KOS_DYN_CAP_NEEDS_UNWIND_ACTION
          | KOS_DYN_CAP_NEEDS_INSTALL_WIND;
    else
      caps = KOS_DYN_CAP_CATCH | KOS_DYN_CAP_NEEDS_UNWIND_ACTION;

    kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, parent, caps);
  }

  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_SHOT(node) = shot;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  return node;
}


static void kos_dyn_with_shot_after_exit_done(KosDynWith *node) {
  if (KosDynWith_SHOT(node) == KOS_DYN_WITH_SHOT_SINGLE_ACTIVE)
    KosDynWith_SHOT(node) = KOS_DYN_WITH_SHOT_SINGLE_DONE;
}


/**
   Clone mgr/enter/exit/name for continuation segment (not in pending await).
*/
KosDynWith *kos_dyn_with_clone(KosDynWith *src) {
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
  KosDynWith_SHOT(node) = KosDynWith_SHOT(src);
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  if (KosDynNode_STACK((KosDynNode *) src)) {
    KosDynNode_STACK((KosDynNode *) node) =
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) src));
    if (! KosDynNode_STACK((KosDynNode *) node)) {
      kos_dynwith_free(node);
      return NULL;
    }
  }
  return node;
}


/**
   DECREF mgr/enter/exit/name and pending exc triple; return to freelist.
*/
static void kos_dynwith_free(KosDynWith *node) {
  if (! node)
    return;

  kos_dynnode_release_stack((KosDynNode *) node);
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
   Push WITH as DYN_TOP with ENTERED=1 after sync __enter__.
*/
KosDynWith *kos_dyn_push_with(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynWith *node = kos_dyn_with_allocate(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc, shot);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 1;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  KosEvalState_SyncFromContext(es);
  return node;
}


KosDynWith *kos_dyn_push_dyn_with(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  return kos_dyn_push_with(
      es, mgr, enter, exit, name, resume_pc, KOS_DYN_WITH_SHOT_MULTI);
}


/**
   Push provisional node before async __enter__ completes (PENDING_ENTER_AWAIT).
*/
KosDynWith *kos_dyn_push_with_provisional(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynWith *node = kos_dyn_with_allocate(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, name, resume_pc, shot);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;

  KosEvalState_SyncFromContext(es);
  return node;
}


KosDynWith *kos_dyn_push_dyn_with_provisional(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  return kos_dyn_push_with_provisional(
      es, mgr, enter, exit, name, resume_pc, KOS_DYN_WITH_SHOT_MULTI);
}


/**
   Unlink WITH when top without running __exit__ (fast path only).
*/
void kos_dyn_pop_with(KosEvalState *es, KosDynWith *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    KosEvalState_SyncFromContext(es);
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynwith_free(node);
  kos_ctx_refresh_dyn_caches(ctx);
  KosEvalState_SyncFromContext(es);
}


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)
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
static int kos_dyn_with_on_enter_control_returned(KosEvalState *es, KosDynWith *with_node) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_ENTER_AWAIT) {
    KosEvalState_SyncFromContext(es);
    return 0;
  }

  if (! KosContext_FRAME_TOP(ctx)) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  {
    PyObject *result = KosEvalState_StackPopRef(es);
    if (! result) {
      KosEvalState_SyncFromContext(es);
      return -1;
    }

    PyObject *name = KosDynWith_NAME(with_node);
    if (name && name != Py_None) {
      if (frame_ns_store(ctx, name, result) < 0) {
        Py_DECREF(result);
        KosEvalState_SyncFromContext(es);
        return -1;
      }
    }
    Py_DECREF(result);
  }

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  KosEvalState_SyncFromContext(es);
  return 0;
}


/**
   Resume after awaitable __exit__: merge result with exc triple, maybe pop node.
*/
static int kos_dyn_with_on_control_returned(KosEvalState *es, KosDynWith *with_node,
                                            int do_pop_node) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    KosEvalState_SyncFromContext(es);
    return 0;
  }

  if (! KosContext_FRAME_TOP(ctx)) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }

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
    kos_dynnode_release_stack((KosDynNode *) with_node);
    kos_ctx_refresh_dyn_caches(ctx);
    if (KosContext_FRAME_TOP(ctx))
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
    if (do_pop_node) {
      kos_dyn_with_shot_after_exit_done(with_node);
      kos_dyn_pop_with(es, with_node);
    }
    if (KosEvalState_StackPushRef(es, Py_None) < 0) {
      KosEvalState_SyncFromContext(es);
      return -1;
    }
    KosEvalState_SyncFromContext(es);
    return 0;
  }

  if (had_exception) {
    Py_DECREF(result);
    kos_dynnode_release_stack((KosDynNode *) with_node);
    kos_ctx_refresh_dyn_caches(ctx);
    if (do_pop_node) {
      kos_dyn_with_shot_after_exit_done(with_node);
      kos_dyn_pop_with(es, with_node);
    }
    PyErr_Restore(typ, val, tb);
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  Py_DECREF(result);
  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  PyObject *body_val = KosEvalState_StackPopRef(es);
  if (! body_val) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }
  kos_dynnode_release_stack((KosDynNode *) with_node);
  kos_ctx_refresh_dyn_caches(ctx);
  if (KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
  if (do_pop_node) {
    kos_dyn_with_shot_after_exit_done(with_node);
    kos_dyn_pop_with(es, with_node);
  }
  if (KosEvalState_StackPushRef(es, body_val) < 0) {
    Py_DECREF(body_val);
    KosEvalState_SyncFromContext(es);
    return -1;
  }
  Py_DECREF(body_val);
  KosEvalState_SyncFromContext(es);
  return 0;
}


/**
   INSTALL wind: sync __enter__ during transfer. Callers: kos_dyn_wind.
*/
static int kos_dyn_dyn_with_wind(KosEvalState *es, KosDynWith *node, KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (KosDynWith_SHOT(node) == KOS_DYN_WITH_SHOT_SINGLE_DONE) {
    KosEvalState_SyncFromContext(es);
    return 0;
  }

  PyObject *enter_fn = KosDynWith_ENTER(node);
  int r = KosEvalState_CallNoArgs(es, out_ev, enter_fn, 0);
  if (r == 1) {
    KosEvalState_SyncFromContext(es);
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  }
  if (r < 0) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  {
    PyObject *result = KosEvalState_StackPopRef(es);
    if (! result) {
      KosEvalState_SyncFromContext(es);
      return -1;
    }

    PyObject *name = KosDynWith_NAME(node);
    if (name && name != Py_None) {
      if (frame_ns_store(ctx, name, result) < 0) {
        Py_DECREF(result);
        KosEvalState_SyncFromContext(es);
        return -1;
      }
    }
    Py_DECREF(result);
  }

  KosDynWith_ENTERED(node) = 1;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosEvalState_SyncFromContext(es);
  return 0;
}


/**
   Run context manager __exit__ during kos_dyn_unwind.
*/
int kos_dyn_with_unwind(
    KosEvalState *es,
    KosDynWith *node,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (KosDynWith_SHOT(node) == KOS_DYN_WITH_SHOT_SINGLE_DONE) {
    KosEvalState_SyncFromContext(es);
    return KOS_DYN_UNWIND_PROTOCOL_SKIPPED;
  }

  PyObject *exit_fn = KosDynWith_EXIT(node);
  PyObject *typ = NULL;
  PyObject *val = NULL;
  PyObject *tb = NULL;

  if (PyErr_Occurred()) {
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_Clear();
    if (! typ) { Py_INCREF(Py_None); typ = Py_None; }
    if (! val) { Py_INCREF(Py_None); val = Py_None; }
    if (! tb)  { Py_INCREF(Py_None); tb  = Py_None; }

  } else {
    Py_INCREF(Py_None); typ = Py_None;
    Py_INCREF(Py_None); val = Py_None;
    Py_INCREF(Py_None); tb  = Py_None;
  }

  PyObject *args = PyTuple_Pack(3, typ, val, tb);
  if (! args) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  int r = KosEvalState_CallArgs(es, out_ev, exit_fn, args, NULL, 0);
  Py_DECREF(args);

  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_EXIT_AWAIT;
    KosDynWith_EXC_TYP(node) = typ;
    KosDynWith_EXC_VAL(node) = val;
    KosDynWith_EXC_TB(node) = tb;
    KosDynWith_NORMAL_EXIT(node) = (typ == Py_None);
    KosEvalState_SyncFromContext(es);
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  }

  if (r < 0) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  if (! (KosContext_BLOCK(ctx) && *KosContext_BLOCK(ctx) ? *KosContext_BLOCK(ctx)
                                                         : NULL)) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  if (typ != Py_None && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    Py_DECREF(result);
    kos_dyn_with_shot_after_exit_done(node);

    kos_dynnode_release_stack((KosDynNode *) node);
    kos_ctx_refresh_dyn_caches(ctx);

    if (KosContext_FRAME_TOP(ctx))
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(node);

    if (KosEvalState_StackPushRef(es, Py_None) < 0) {
      KosEvalState_SyncFromContext(es);
      return -1;
    }

    KosEvalState_SyncFromContext(es);
    return KOS_DYN_UNWIND_SWALLOWED;

  } else if (typ != Py_None) {
    PyErr_Restore(typ, val, tb);

  } else {
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
  }

  Py_DECREF(result);
  kos_dyn_with_shot_after_exit_done(node);
  KosEvalState_SyncFromContext(es);
  return 0;
}


/**
   Complete provisional WITH after __enter__ result is ready (non-await path).
*/
int finalize_provisional_with_enter(KosEvalState *es, PyObject *result,
                                    KosDynWith *with_node) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      KosEvalState_SyncFromContext(es);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  KosEvalState_SyncFromContext(es);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_WITH: visit mgr, enter, exit, and name.
*/
static int dynnode_traverse_with(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWith *with_node = (KosDynWith *) node;
  {
    int tr = kos_stackblock_traverse(KosDynNode_STACK(node), visit, arg);
    if (tr != 0)
      return tr;
  }
  Py_VISIT(KosDynWith_MGR(with_node));
  Py_VISIT(KosDynWith_ENTER(with_node));
  Py_VISIT(KosDynWith_EXIT(with_node));
  Py_VISIT(KosDynWith_NAME(with_node));
  if (KosDynWith_PHASE(with_node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    Py_VISIT(KosDynWith_EXC_TYP(with_node));
    Py_VISIT(KosDynWith_EXC_VAL(with_node));
    Py_VISIT(KosDynWith_EXC_TB(with_node));
  }
  return 0;
}


// The end.
