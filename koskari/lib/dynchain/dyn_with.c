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
  KosSegment *stack;

  if (! node)
    return NULL;

  stack = KosStack_NewEmpty();
  if (! stack) {
    kos_dynnode_return_to_freelist((KosDynNode *) node);
    return NULL;
  }

  {
    uint32_t caps;
    if (shot == KOS_DYN_WITH_SHOT_MULTI)
      caps = KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION | KOS_DYN_CAP_NEEDS_UNWIND_ACTION
          | KOS_DYN_CAP_NEEDS_INSTALL_WIND;
    else
      caps = KOS_DYN_CAP_CATCH | KOS_DYN_CAP_NEEDS_UNWIND_ACTION;

    kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, parent, caps);
  }

  KosDynNode_STACK((KosDynNode *) node) = stack;
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_SHOT(node) = shot;
  KosDynWith_EXIT_DONE(node) = NULL;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;

  /* Single-shot: the exit-done bit lives in a KosRef cell shared across
     continuation clones (non-empty once __exit__ has run) so a captured
     twin cannot re-run a discharged manager. */
  if (shot == KOS_DYN_WITH_SHOT_SINGLE_ACTIVE) {
    KosDynWith_EXIT_DONE(node) = KosRef_New(NULL, NULL);
    if (! KosDynWith_EXIT_DONE(node)) {
      KosStack_Release(stack);
      KosDynNode_STACK((KosDynNode *) node) = NULL;
      kos_dynnode_return_to_freelist((KosDynNode *) node);
      return NULL;
    }
  }
  return node;
}


static void kos_dyn_with_shot_after_exit_done(KosDynWith *node) {
  if (KosDynWith_SHOT(node) == KOS_DYN_WITH_SHOT_SINGLE_ACTIVE) {
    KosDynWith_SHOT(node) = KOS_DYN_WITH_SHOT_SINGLE_DONE;
    if (KosDynWith_EXIT_DONE(node))
      KosRef_Store(KosDynWith_EXIT_DONE(node), Py_True);
  }
}


/**
   True when this node's single-shot __exit__ has already run, on this
   node or on any clone sharing its exit-done cell. Multi-shot is never
   done.
*/
static int kos_dyn_with_single_exit_done(KosDynWith *node) {
  if (KosDynWith_SHOT(node) == KOS_DYN_WITH_SHOT_MULTI)
    return 0;
  if (KosDynWith_SHOT(node) == KOS_DYN_WITH_SHOT_SINGLE_DONE)
    return 1;
  if (KosDynWith_EXIT_DONE(node))
    return KosRef_IsEmpty(KosDynWith_EXIT_DONE(node)) == 0;
  return 0;
}


static int kos_dyn_with_post_unwind_deliver(
    KosEvalState *es,
    KosDynWith *with_node) {

  KosSegment *stack = KosDynNode_STACK((KosDynNode *) with_node);
  if (! stack || KosSegment_LEN(stack) == 0)
    return 0;

  PyObject *body_val = KosStack_PopRef(&KosDynNode_STACK((KosDynNode *) with_node));
  if (! body_val)
    return -1;
  if (KosEvalState_ParentStackPushRef(es, body_val) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  Py_DECREF(body_val);
  return 0;
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
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_SHOT(node) = KosDynWith_SHOT(src);
  /* Share (not copy) the single-shot exit-done cell: the live twin's exit
     must be visible to every clone of this extent. */
  KosDynWith_EXIT_DONE(node) = KosDynWith_EXIT_DONE(src);
  Py_XINCREF(KosDynWith_EXIT_DONE(node));
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_EXC_TYP(node) = NULL;
  KosDynWith_EXC_VAL(node) = NULL;
  KosDynWith_EXC_TB(node) = NULL;
  if (KosDynNode_STACK((KosDynNode *) src)) {
    KosDynNode_STACK((KosDynNode *) node) =
        KosStack_DeepCopy(KosDynNode_STACK((KosDynNode *) src));
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

  Py_XDECREF(KosDynWith_MGR(node));
  KosDynWith_MGR(node) = NULL;
  Py_XDECREF(KosDynWith_ENTER(node));
  KosDynWith_ENTER(node) = NULL;
  Py_XDECREF(KosDynWith_EXIT(node));
  KosDynWith_EXIT(node) = NULL;
  Py_XDECREF(KosDynWith_NAME(node));
  KosDynWith_NAME(node) = NULL;
  Py_XDECREF(KosDynWith_EXC_TYP(node));
  KosDynWith_EXC_TYP(node) = NULL;
  Py_XDECREF(KosDynWith_EXC_VAL(node));
  KosDynWith_EXC_VAL(node) = NULL;
  Py_XDECREF(KosDynWith_EXC_TB(node));
  KosDynWith_EXC_TB(node) = NULL;
  Py_XDECREF(KosDynWith_EXIT_DONE(node));
  KosDynWith_EXIT_DONE(node) = NULL;

  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_with(KosDynNode *node, PyObject *data) {

  KosDynWith *with_node = (KosDynWith *) node;
  if (snapshot_set_obj(data, "mgr", KosDynWith_MGR(with_node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "enter", KosDynWith_ENTER(with_node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "exit", KosDynWith_EXIT(with_node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "name", KosDynWith_NAME(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "entered", KosDynWith_ENTERED(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "phase", (int) KosDynWith_PHASE(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "shot", (int) KosDynWith_SHOT(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "exit_done",
      kos_dyn_with_single_exit_done(with_node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "resume_pc", KosDynWith_RESUME_PC(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "normal_exit", KosDynWith_NORMAL_EXIT(with_node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "exc_typ", KosDynWith_EXC_TYP(with_node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "exc_val", KosDynWith_EXC_VAL(with_node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "exc_tb", KosDynWith_EXC_TB(with_node)) < 0)
    return -1;
  return 0;
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

  // CONTEXT: dirty, touches context directly
  KosDynWith *node = kos_dyn_with_allocate(
      KosEvalState_DYN_TOP(es), mgr, enter, exit, name, resume_pc, shot);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 1;
  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynwith_free(node);
    return NULL;
  }
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

  KosDynWith *node = kos_dyn_with_allocate(
      KosEvalState_DYN_TOP(es), mgr, enter, exit, name, resume_pc, shot);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynwith_free(node);
    return NULL;
  }
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
int kos_dyn_pop_with(KosEvalState *es, KosDynWith *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "WITH node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynwith_free(node);
  }
  return 0;
}


/**
   Bind name in frame namespace for `as` targets.
*/
static int frame_ns_store(KosEvalState *es, PyObject *name, PyObject *val) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);
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

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_ENTER_AWAIT) {
    return 0;
  }

  if (! KosEvalState_FRAME(es)) {
    return -1;
  }

  {
    PyObject *result = KosEvalState_StackPopRef(es);
    if (! result) {
      return -1;
    }

    PyObject *name = KosDynWith_NAME(with_node);
    if (name && name != Py_None) {
      if (frame_ns_store(es, name, result) < 0) {
        Py_DECREF(result);
        return -1;
      }
    }
    Py_DECREF(result);
  }

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/**
   Resume after awaitable __exit__: merge result with exc triple, maybe pop node.
*/
static int kos_dyn_with_on_control_returned(KosEvalState *es, KosDynWith *with_node,
                                            int do_pop_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    return 0;
  }

  if (! KosEvalState_FRAME(es)) {
    return -1;
  }

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
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
    if (KosEvalState_FRAME(es))
      KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(with_node));
    if (do_pop_node) {
      kos_dyn_with_shot_after_exit_done(with_node);
      if (kos_dyn_pop_with(es, with_node) < 0)
        return -1;
    }
    /* Every caller pops, so the node is gone and its owning frame's
       block is the current stack; a parent-stack push would walk past
       the very frame that is about to receive the value. */
    if (KosEvalState_StackPushRef(es, Py_None) < 0)
      return -1;
    return 0;
  }

  if (had_exception) {
    Py_DECREF(result);
    if (do_pop_node) {
      kos_dyn_with_shot_after_exit_done(with_node);
      if (kos_dyn_pop_with(es, with_node) < 0)
        return -1;
    }
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  Py_DECREF(result);
  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  if (kos_dyn_with_post_unwind_deliver(es, with_node) < 0) {
    return -1;
  }
  if (KosEvalState_FRAME(es))
    KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(with_node));
  if (do_pop_node) {
    kos_dyn_with_shot_after_exit_done(with_node);
    if (kos_dyn_pop_with(es, with_node) < 0)
      return -1;
  }
  return 0;
}


/**
   INSTALL wind: sync __enter__ during transfer. Callers: kos_dyn_wind.
*/
static KosOpResult kos_dyn_dyn_with_wind(KosEvalState *es, KosDynWith *node) {
  if (kos_dyn_with_single_exit_done(node)) {
    return EVAL_NORMAL();
  }

  PyObject *enter_fn = KosDynWith_ENTER(node);

  /* A KosCallable does not run here: its install configures a frame that
     the dispatch loop enters once we hand control back, so nothing has
     been pushed and the completion below must not run. */
  const int installs_frame = KosCallable_Check(enter_fn);

  KosOpResult kr = KosEvalState_CallNoArgs(es, enter_fn, 0);
  if (kr.kind == KOS_RES_RAISE) {
    return EVAL_RAISE();
  }
  if (kr.kind != KOS_RES_NORMAL || installs_frame) {
    /* __enter__ has not returned: it suspended, or its frame is installed
       and pending. Leave the node in PENDING_ENTER_AWAIT for
       kos_dynnode_on_control_returned, which binds the result when the
       call actually returns. */
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
    return (kr.kind == KOS_RES_NORMAL) ? EVAL_REPROLOGUE() : kr;
  }

  {
    PyObject *result = KosEvalState_StackPopRef(es);
    if (! result) {
      return EVAL_RAISE();
    }

    PyObject *name = KosDynWith_NAME(node);
    if (name && name != Py_None) {
      if (frame_ns_store(es, name, result) < 0) {
        Py_DECREF(result);
        return EVAL_RAISE();
      }
    }
    Py_DECREF(result);
  }

  KosDynWith_ENTERED(node) = 1;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  return EVAL_NORMAL();
}


/**
   Run context manager __exit__ during kos_dyn_unwind.
   Returns EVAL_AWAIT when __exit__ returned an awaitable (payload in kr.val).
   Returns EVAL_REPROLOGUE when the exit was swallowed (exception suppressed,
   resume_pc updated and Py_None pushed to parent stack; kos_dyn_unwind maps
   this to EVAL_NORMAL so the node is popped before REPROLOGUE is returned).
   Returns EVAL_NORMAL on ordinary success.
   Returns EVAL_RAISE on error.
*/
KosOpResult kos_dyn_with_unwind(KosEvalState *es, KosDynWith *node) {

  if (KosDynWith_EXIT_PENDING(node)) {
    /* __exit__ was already dispatched; an unwind can only arrive back
       here when it completed by raising instead of returning through
       kos_dynnode_on_control_returned. The manager is discharged: its
       exception replaces the saved one and must never re-fire
       __exit__. NORMAL with the error left pending pops the node and
       lets the unwind continue past it. */
    Py_CLEAR(KosDynWith_EXC_TYP(node));
    Py_CLEAR(KosDynWith_EXC_VAL(node));
    Py_CLEAR(KosDynWith_EXC_TB(node));
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
    kos_dyn_with_shot_after_exit_done(node);
    return EVAL_NORMAL();
  }

  if (kos_dyn_with_single_exit_done(node)) {
    /* __exit__ already ran, possibly on a twin sharing the exit-done
       cell; complete the extent without re-firing it. */
    if (! PyErr_Occurred()) {
      if (KosEvalState_FRAME(es))
        KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(node));
      if (kos_dyn_with_post_unwind_deliver(es, node) < 0)
        return EVAL_RAISE();
    }
    return EVAL_NORMAL();
  }

  PyObject *exit_fn = KosDynWith_EXIT(node);
  const int installs_frame = KosCallable_Check(exit_fn);
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
    return EVAL_RAISE();
  }

  KosOpResult kr = KosEvalState_CallArgs(es, exit_fn, args, NULL, 0);
  Py_DECREF(args);

  if (kr.kind == KOS_RES_RAISE) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return EVAL_RAISE();
  }

  if (kr.kind != KOS_RES_NORMAL || installs_frame) {
    /* __exit__ has not returned: suspended, or its frame is installed and
       pending. The node keeps the exception triple and stays on the
       chain; kos_dynnode_on_control_returned merges the result and pops
       it once the call actually returns. */
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_EXIT_AWAIT;
    KosDynWith_EXC_TYP(node) = typ;
    KosDynWith_EXC_VAL(node) = val;
    KosDynWith_EXC_TB(node) = tb;
    KosDynWith_NORMAL_EXIT(node) = (typ == Py_None);
    return (kr.kind == KOS_RES_NORMAL) ? EVAL_REPROLOGUE() : kr;
  }

  if (! (KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)
             ? *KosEvalState_BLOCK(es)
             : NULL)) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return EVAL_RAISE();
  }

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
    Py_DECREF(typ);
    Py_DECREF(val);
    Py_DECREF(tb);
    return EVAL_RAISE();
  }

  if (typ != Py_None && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    Py_DECREF(result);
    kos_dyn_with_shot_after_exit_done(node);

    if (KosEvalState_FRAME(es))
      KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(node));

    if (KosEvalState_ParentStackPushRef(es, Py_None) < 0) {
      return EVAL_RAISE();
    }

    return EVAL_REPROLOGUE();

  } else if (typ != Py_None) {
    PyErr_Restore(typ, val, tb);

  } else {
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
  }

  Py_DECREF(result);
  kos_dyn_with_shot_after_exit_done(node);
  if (! PyErr_Occurred()) {
    if (KosEvalState_FRAME(es))
      KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(node));
    if (kos_dyn_with_post_unwind_deliver(es, node) < 0) {
      return EVAL_RAISE();
    }
  }
  return EVAL_NORMAL();
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_WITH: visit mgr, enter, exit, and name.
*/
static int dynnode_traverse_with(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWith *with_node = (KosDynWith *) node;
  {
    int tr = KosStack_Traverse(KosDynNode_STACK(node), visit, arg);
    if (tr != 0)
      return tr;
  }
  Py_VISIT(KosDynWith_MGR(with_node));
  Py_VISIT(KosDynWith_ENTER(with_node));
  Py_VISIT(KosDynWith_EXIT(with_node));
  Py_VISIT(KosDynWith_EXIT_DONE(with_node));
  Py_VISIT(KosDynWith_NAME(with_node));
  if (KosDynWith_PHASE(with_node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
    Py_VISIT(KosDynWith_EXC_TYP(with_node));
    Py_VISIT(KosDynWith_EXC_VAL(with_node));
    Py_VISIT(KosDynWith_EXC_TB(with_node));
  }
  return 0;
}


// The end.
