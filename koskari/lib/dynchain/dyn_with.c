/* Included by dynchain.c — dyn_with */
/* === KosDynWith (context manager; multi/single via u.with.shot) === */


static void kos_dynwith_free(KosDynWith *node);
static void kos_dyn_with_abort_impl(KosDynWith *node);


/**
   Allocate a WITH node: kind KOS_DYN_WITH, caps from shot (multi vs single).
*/
static KosDynWith *kos_dyn_with_allocate(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    uint8_t self_bind,
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
          | KOS_DYN_CAP_NEEDS_INSTALL_WIND | KOS_DYN_CAP_CONTROL_HOOK;
    else
      caps = KOS_DYN_CAP_CATCH | KOS_DYN_CAP_NEEDS_UNWIND_ACTION
          | KOS_DYN_CAP_CONTROL_HOOK;

    kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, parent, caps);
  }

  kos_dynnode_use_inline_segment((KosDynNode *) node);
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_SELF_BIND(node) = self_bind;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_SHOT(node) = shot;
  KosDynWith_EXIT_DONE(node) = NULL;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_OB(node)->kind = KOS_OB_NONE;
  KosDynWith_SWALLOWED(node) = 0;

  /* Single-shot: the exit-done bit lives in the node's own shot field
     until a continuation clones the node; the clone then makes the
     shared cell (see kos_dyn_with_clone). */
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


/**
   Clone mgr/enter/exit/name for continuation segment (not in pending await).
*/
KosDynWith *kos_dyn_with_clone(KosDynWith *src) {
  if (! src)
    return NULL;
  /* A pending node has an __enter__ or __exit__ in flight, which no
     template can carry; nor can what was parked for its completion. */
  if (KosDynWith_ENTER_PENDING(src) || KosDynWith_EXIT_PENDING(src)
      || KosDynWith_OB(src)->kind != KOS_OB_NONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot capture a continuation across a with whose"
                    " __enter__ or __exit__ is in flight");
    return NULL;
  }

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
  KosDynWith_SELF_BIND(node) = KosDynWith_SELF_BIND(src);
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_SHOT(node) = KosDynWith_SHOT(src);
  /* A single-shot extent's exit-done fact is shared, not copied: the
     live twin's exit must be visible to every clone. The cell is made
     here, on first clone, so a with that is never captured (nearly all
     of them) allocates nothing; every reader treats a missing cell as
     not done and the node's own shot field still records the live
     twin's exit. */
  if (KosDynWith_SHOT(src) == KOS_DYN_WITH_SHOT_SINGLE_ACTIVE
      && ! KosDynWith_EXIT_DONE(src)) {
    KosDynWith_EXIT_DONE(src) = KosRef_New(NULL, NULL);
    if (! KosDynWith_EXIT_DONE(src)) {
      kos_dynwith_free(node);
      return NULL;
    }
  }
  KosDynWith_EXIT_DONE(node) = KosDynWith_EXIT_DONE(src);
  Py_XINCREF(KosDynWith_EXIT_DONE(node));
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_OB(node)->kind = KOS_OB_NONE;
  KosDynWith_SWALLOWED(node) = 0;
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
  kos_dyn_ob_clear(KosDynWith_OB(node));
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
  if (snapshot_set_int(data, "self_bind",
      (int) KosDynWith_SELF_BIND(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "exit_done",
      kos_dyn_with_single_exit_done(with_node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "resume_pc", KosDynWith_RESUME_PC(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "swallowed", KosDynWith_SWALLOWED(with_node)) < 0)
    return -1;
  if (snapshot_set_int(data, "obligation",
      (int) KosDynWith_OB(with_node)->kind) < 0)
    return -1;
  return 0;
}


/**
   Push provisional node before async __enter__ completes (PENDING_ENTER_AWAIT).
*/
KosDynWith *kos_dyn_push_with_provisional(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    uint8_t self_bind,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot) {

  KosDynWith *node = kos_dyn_with_allocate(
      KosEvalState_DYN_TOP(es), mgr, enter, exit, self_bind, name,
      resume_pc, shot);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynwith_free(node);
    return NULL;
  }
  return node;
}


/**
   Roll back a WITH that never entered: __enter__ raised. The node is
   dyn_top and not ENTERED, so the unlink runs no __exit__.
*/
int kos_dyn_pop_with(KosEvalState *es, KosDynWith *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "WITH node is not at top of chain");
    return -1;
  }
  {
    KosOpResult r = kos_dyn_unlink_top(es, KOS_XFER_EVENT_NONE, 0);
    if (r.kind == KOS_RES_NORMAL)
      return 0;
    /* an __exit__ started here is a caller error: the node was not to
       be entered */
    Py_XDECREF(r.val);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_SystemError,
                      "kos_dyn_pop_with: __exit__ ran on a rollback");
    return -1;
  }
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
   Complete an __enter__ whose result is TOS: pop it, bind the with name
   when there is one, and mark the node entered and idle.
*/
static int kos_dyn_with_bind_entered(KosEvalState *es, KosDynWith *node) {
  PyObject *result = KosEvalState_StackPopRef(es);
  PyObject *name;

  if (! result)
    return -1;

  name = KosDynWith_NAME(node);
  if (name && name != Py_None && frame_ns_store(es, name, result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(node) = 1;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/**
   Classify __exit__'s result against the triple parked on the node. A
   truthy result against an exception swallows it: the triple is
   released and the node remembers to deliver None. Any other outcome
   leaves the triple where it is for the unlink to re-raise.
*/
static void kos_dyn_with_classify_exit(
    KosEvalState *es,
    KosDynWith *node,
    PyObject *result) {

  KosObligation *ob = KosDynWith_OB(node);
  int handling = (ob->kind == KOS_OB_EXCEPTION);

  if (handling && PyObject_IsTrue(result)) {
    PyErr_Clear();
    kos_dyn_ob_clear(ob);
    KosDynWith_SWALLOWED(node) = 1;
    if (KosEvalState_FRAME(es))
      KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(node));
  } else if (! handling) {
    if (KosEvalState_FRAME(es))
      KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(node));
  }
  Py_DECREF(result);
  kos_dyn_with_shot_after_exit_done(node);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_LEFT;
}


/**
   Enter: run __enter__ once. See the extent contract in dynchain.h.
*/
static KosOpResult kos_dyn_with_enter(KosEvalState *es, KosDynWith *node) {

  if (KosDynWith_LEFT(node) || kos_dyn_with_single_exit_done(node)
      || KosDynWith_ENTERED(node))
    return EVAL_NORMAL();
  if (KosDynWith_ENTER_PENDING(node))
    return EVAL_REPROLOGUE();  /* __enter__ in flight */

  PyObject *enter_fn = KosDynWith_ENTER(node);
  KosDynFrame *const caller = KosEvalState_FRAME(es);

  /* A plain function resolved on the type is called with the manager as
     its receiver; anything else was bound at resolution. */
  KosOpResult kr = (KosDynWith_SELF_BIND(node) & KOS_WITH_SELF_ENTER)
      ? KosEvalState_Call(es, enter_fn, &KosDynWith_MGR(node), 1, NULL, 0)
      : KosEvalState_CallNoArgs(es, enter_fn, 0);
  if (kr.kind == KOS_RES_RAISE)
    return EVAL_RAISE();

  /* A callee whose frame is now current has not run, and an awaitable
     has not settled: nothing has been pushed, and the completion below
     must not run. A callable that completed in place has pushed its
     value. */
  if (kr.kind != KOS_RES_NORMAL || KosEvalState_FRAME(es) != caller) {
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
    return (kr.kind == KOS_RES_NORMAL) ? EVAL_REPROLOGUE() : kr;
  }

  return kos_dyn_with_bind_entered(es, node) < 0 ? EVAL_RAISE() : EVAL_NORMAL();
}


/**
   Leave: run __exit__ once. See the extent contract in dynchain.h. The
   body value and a re-raise are not delivered here; the unlink does that
   after the node is gone.
*/
static KosOpResult kos_dyn_with_leave(
    KosEvalState *es,
    KosDynWith *node,
    KosXferEvent ev) {

  (void) ev;

  if (KosDynWith_LEFT(node))
    return EVAL_NORMAL();

  if (KosDynWith_EXIT_PENDING(node) || KosDynWith_ENTER_PENDING(node)) {
    /* A call in flight sits in a frame above this node. If this node
       is dyn_top, that frame is gone (it raised, or the walk crossed
       it): the manager is discharged with nothing to take, and an
       __exit__ that raised has replaced the exception it was handling. */
    if (KosEvalState_DYN_TOP(es) != (KosDynNode *) node)
      return EVAL_REPROLOGUE();
    kos_dyn_with_abort_impl(node);
    return EVAL_NORMAL();
  }

  if (! KosDynWith_ENTERED(node)) {
    /* never entered: no __exit__ */
    kos_dyn_with_abort_impl(node);
    return EVAL_NORMAL();
  }

  if (kos_dyn_with_single_exit_done(node)) {
    /* __exit__ already ran, possibly on a twin sharing the exit-done
       cell; complete the extent without re-firing it. */
    if (! PyErr_Occurred() && KosEvalState_FRAME(es))
      KosEvalState_SET_PC(es, KosDynWith_RESUME_PC(node));
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_LEFT;
    return EVAL_NORMAL();
  }

  PyObject *exit_fn = KosDynWith_EXIT(node);
  KosDynFrame *const caller = KosEvalState_FRAME(es);
  KosObligation *ob = KosDynWith_OB(node);

  /* The exception in flight parks on the node as the obligation __exit__
     is handling: __exit__ may suspend, and the unlink re-raises it. A
     normal exit passes None three times. */
  if (PyErr_Occurred()) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    if (kos_dyn_ob_park_exception((KosDynNode *) node, typ, val, tb) < 0)
      return EVAL_RAISE();
  }

  /* The exception triple as a vector, with the manager in front when
     __exit__ is a plain function resolved on the type. */
  PyObject *argv[4];
  Py_ssize_t argc = 0;
  if (KosDynWith_SELF_BIND(node) & KOS_WITH_SELF_EXIT)
    argv[argc++] = KosDynWith_MGR(node);
  if (ob->kind == KOS_OB_EXCEPTION) {
    argv[argc++] = ob->u.exc.type ? ob->u.exc.type : Py_None;
    argv[argc++] = ob->u.exc.value ? ob->u.exc.value : Py_None;
    argv[argc++] = ob->u.exc.tb ? ob->u.exc.tb : Py_None;
  } else {
    argv[argc++] = Py_None;
    argv[argc++] = Py_None;
    argv[argc++] = Py_None;
  }

  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_EXIT_AWAIT;
  KosOpResult kr = KosEvalState_Call(es, exit_fn, argv, argc, NULL, 0);

  if (kr.kind == KOS_RES_RAISE) {
    /* __exit__ itself raised; its exception supersedes the one it was
       handling, which the unlink chains in as context. Leave the pending
       phase: the raise walk aborts it. */
    return EVAL_RAISE();
  }

  if (kr.kind != KOS_RES_NORMAL || KosEvalState_FRAME(es) != caller) {
    /* __exit__ has not returned: suspended, or its frame is installed
       and current. The node keeps the triple and stays on the chain. */
    return (kr.kind == KOS_RES_NORMAL) ? EVAL_REPROLOGUE() : kr;
  }

  if (! (KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)))
    return EVAL_RAISE();

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result)
    return EVAL_RAISE();

  kos_dyn_with_classify_exit(es, node, result);
  return EVAL_NORMAL();
}


/**
   The pending __enter__ or __exit__ returned; its result is the top of
   the live block.
*/
static int kos_dyn_with_settle(KosEvalState *es, KosDynWith *node) {

  if (! KosEvalState_FRAME(es))
    return -1;

  if (KosDynWith_ENTER_PENDING(node))
    return kos_dyn_with_bind_entered(es, node);

  if (KosDynWith_EXIT_PENDING(node)) {
    PyObject *result = KosEvalState_StackPopRef(es);
    if (! result)
      return -1;
    kos_dyn_with_classify_exit(es, node, result);
  }
  return 0;
}


/**
   The pending __enter__ or __exit__ died: the manager is discharged
   with nothing to take.
*/
static void kos_dyn_with_abort_impl(KosDynWith *node) {
  /* what was parked stays for the unlink to chain or discard */
  kos_dyn_with_shot_after_exit_done(node);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_LEFT;
}


static void kos_dyn_with_abort(KosDynWith *node) {

  if (KosDynWith_ENTER_PENDING(node) || KosDynWith_EXIT_PENDING(node))
    kos_dyn_with_abort_impl(node);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_WITH: visit mgr, enter, exit, and name.
*/
static int dynnode_traverse_with(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWith *with_node = (KosDynWith *) node;
  Py_VISIT(KosDynWith_MGR(with_node));
  Py_VISIT(KosDynWith_ENTER(with_node));
  Py_VISIT(KosDynWith_EXIT(with_node));
  Py_VISIT(KosDynWith_EXIT_DONE(with_node));
  Py_VISIT(KosDynWith_NAME(with_node));
  return kos_dyn_ob_traverse(KosDynWith_OB(with_node), visit, arg);
}


// The end.
