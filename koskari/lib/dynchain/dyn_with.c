/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
  Included by dynchain.c. The KOS_DYN_WITH node: the context manager
  extent for with and dyn-with, told apart by KOS_DYN_CAP_WINDS, which a
  dyn-with carries.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynWith (context manager; dyn-with carries KOS_DYN_CAP_WINDS) === */


static void kos_dynwith_free(KosDynWith *node);
static void kos_dyn_with_abort_impl(KosDynWith *node);


/**
   Allocate a WITH node: kind KOS_DYN_WITH. winds is KOS_DYN_CAP_WINDS
   for a dyn-with, which a transfer runs, and 0 for a plain with, which
   is local to its body.
*/
static KosDynWith *kos_dyn_with_allocate(
    KosDynNode *parent,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    uint8_t self_bind,
    PyObject *name,
    KosOp *resume_pc,
    uint32_t winds) {

  KosDynWith *node = (KosDynWith *) kos_dynnode_alloc();

  if (! node)
    return NULL;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_WITH, parent,
                   KOS_DYN_CAP_CATCH | KOS_DYN_CAP_CONTROL_HOOK
                   | (winds & KOS_DYN_CAP_WINDS));
  kos_dynnode_use_inline_segment((KosDynNode *) node);
  KosDynWith_MGR(node) = mgr;
  KosDynWith_ENTER(node) = enter;
  KosDynWith_EXIT(node) = exit;
  KosDynWith_SELF_BIND(node) = self_bind;
  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = name;
  KosDynWith_RESUME_PC(node) = resume_pc;
  KosDynWith_OB(node)->kind = KOS_OB_NONE;
  KosDynWith_EXC_SAVED(node) = 0;
  KosExc_Init(KosDynWith_SAVED(node));
  return node;
}


/**
   Clone mgr/enter/exit/name for continuation segment (not in pending await).
*/
KosDynWith *kos_dyn_with_clone(KosDynWith *src) {
  if (! src)
    return NULL;
  /* A pending node has an __enter__ or __exit__ in flight, which no
     template can carry; nor can what was stored for its completion. */
  if (KosDynWith_ENTER_PENDING(src) || KosDynWith_EXIT_PENDING(src)
      || KosDynWith_OB(src)->kind != KOS_OB_NONE) {
    kos_raise_prompt_unreachable(
        NULL, KosDynWith_ENTER_PENDING(src) ? "enter" : "exit",
        "cannot capture a continuation from inside a with whose %s is"
        " in flight");
    return NULL;
  }

  KosDynWith *node = (KosDynWith *) kos_dynnode_clone_base(
      (KosDynNode *) src);
  if (! node)
    return NULL;

  KosDynWith_MGR(node) = KosDynWith_MGR(src);
  Py_XINCREF(KosDynWith_MGR(node));
  KosDynWith_ENTER(node) = KosDynWith_ENTER(src);
  Py_XINCREF(KosDynWith_ENTER(node));
  KosDynWith_EXIT(node) = KosDynWith_EXIT(src);
  Py_XINCREF(KosDynWith_EXIT(node));
  KosDynWith_SELF_BIND(node) = KosDynWith_SELF_BIND(src);
  /* A transfer enters a dyn-with's copy afresh. A plain with's copy is
     the extent its body already entered, and runs __exit__ when that
     copy exits locally. */
  KosDynWith_ENTERED(node) = KosDynNode_HAS_CAP(src, KOS_DYN_CAP_WINDS)
      ? 0 : KosDynWith_ENTERED(src);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  KosDynWith_NAME(node) = KosDynWith_NAME(src);
  if (KosDynWith_NAME(node))
    Py_INCREF(KosDynWith_NAME(node));
  KosDynWith_RESUME_PC(node) = KosDynWith_RESUME_PC(src);
  KosDynWith_OB(node)->kind = KOS_OB_NONE;
  KosDynWith_EXC_SAVED(node) = 0;
  KosExc_Init(KosDynWith_SAVED(node));
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
  KosExc_Clear(KosDynWith_SAVED(node));

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
  if (snapshot_set_int(data, "self_bind",
      (int) KosDynWith_SELF_BIND(with_node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "resume_pc",
                        kos_insn_pc(KosDynWith_RESUME_PC(with_node))) < 0)
    return -1;
  if (snapshot_set_int(data, "swallowed",
      KosDynWith_OB(with_node)->kind == KOS_OB_SWALLOWED) < 0)
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
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    uint8_t self_bind,
    PyObject *name,
    KosOp *resume_pc,
    uint32_t winds) {

  KosDynWith *node = kos_dyn_with_allocate(
      KosContext_DYN_TOP(ctx), mgr, enter, exit, self_bind, name,
      resume_pc, winds);
  if (! node)
    return NULL;

  KosDynWith_ENTERED(node) = 0;
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_IDLE;
  if (kos_dyn_push(ctx, (KosDynNode *) node) < 0) {
    kos_dynwith_free(node);
    return NULL;
  }
  return node;
}


/**
   Roll back a WITH that never entered: __enter__ raised. The node is
   dyn_top and not ENTERED, so the unlink runs no __exit__.
*/
int kos_dyn_pop_with(KosContext *ctx, KosDynWith *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "WITH node is not at top of chain");
    return -1;
  }
  {
    KosOpResult r = kos_dyn_unlink_top(ctx, KOS_XFER_EVENT_NONE, 0);
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
   Complete an __enter__ whose result is TOS: pop it, bind the with name
   when there is one, and mark the node entered and idle. The name binds
   in the node's own namespace, the one the with was pushed in: on a
   plain push that is the live namespace too, but a transfer re-enters
   the node with the attached frame above it current, and the binding
   belongs to the with's scope, not to whatever frame sits at dyn_top.
*/
static int kos_dyn_with_bind_entered(KosContext *ctx, KosDynWith *node) {
  PyObject *result = KosContext_StackPopRef(ctx);
  PyObject *name;

  if (! result)
    return -1;

  name = KosDynWith_NAME(node);
  if (name && name != Py_None) {
    PyObject *ns = KosDynNode_NS((KosDynNode *) node);

    if (! KosNamespace_Check(ns)) {
      PyErr_Format(PyExc_TypeError,
                   "with namespace must be Namespace, got %R", ns);
      Py_DECREF(result);
      return -1;
    }
    if (KosNamespace_Store(ns, name, result) < 0) {
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
   __exit__ is done, by any route: give back the handler-visible state
   it displaced. A call that handled nothing displaced nothing.
*/
static void kos_dyn_with_exit_done_exc(KosDynWith *node) {
  if (KosDynWith_EXC_SAVED(node)) {
    KosExc_Install(KosDynWith_SAVED(node));
    KosDynWith_EXC_SAVED(node) = 0;
  }
}


/**
   Classify __exit__'s result against the triple stored on the node. A
   truthy result against an exception swallows it: the triple is
   released and the node now owes None in its place. Any other outcome
   leaves the triple where it is for the unlink to re-raise. Returns -1
   with the error set when the result's truth test raises.
*/
static int kos_dyn_with_classify_exit(
    KosContext *ctx,
    KosDynWith *node,
    PyObject *result) {

  KosObligation *ob = KosDynWith_OB(node);
  int handling = (ob->kind == KOS_OB_EXCEPTION);
  int swallow = 0;

  kos_dyn_with_exit_done_exc(node);

  if (handling) {
    swallow = PyObject_IsTrue(result);
    if (swallow < 0) {
      /* the result's truth test raised: that exception supersedes the
         handled one, as a raising __exit__ does; the node keeps its
         pending phase and stored triple for the raise walk */
      Py_DECREF(result);
      return -1;
    }
  }

  if (swallow) {
    kos_dyn_ob_clear(ob);
    ob->kind = KOS_OB_SWALLOWED;
    if (KosContext_FRAME(ctx))
      KosContext_SET_PC(ctx, KosDynWith_RESUME_PC(node));
  } else if (! handling) {
    if (KosContext_FRAME(ctx))
      KosContext_SET_PC(ctx, KosDynWith_RESUME_PC(node));
  }
  Py_DECREF(result);
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_LEFT;
  return 0;
}


/**
   Enter: run __enter__ once. See the extent contract in dynchain.h.
*/
static KosOpResult kos_dyn_with_enter(KosContext *ctx, KosDynWith *node) {

  if (KosDynWith_LEFT(node) || KosDynWith_ENTERED(node))
    return EVAL_NORMAL();
  if (KosDynWith_ENTER_PENDING(node))
    return EVAL_REPROLOGUE();  /* __enter__ in flight */

  PyObject *enter_fn = KosDynWith_ENTER(node);

  /* A plain function resolved on the type is called with the manager as
     its receiver; anything else was bound at resolution. */
  KosOpResult kr = (KosDynWith_SELF_BIND(node) & KOS_WITH_SELF_ENTER)
      ? KosContext_Call(
            ctx, enter_fn, &KosDynWith_MGR(node), 1, NULL, 0, NULL, NULL)
      : KosContext_Call(ctx, enter_fn, NULL, 0, NULL, 0, NULL, NULL);

  /* A value in hand is pushed on the block, or is awaited there; settled,
     the entry completes now. */
  if (kr.kind == KOS_RES_RETURN) {
    kr = KosContext_PushOrAwait(ctx, kr.val);
    if (kr.kind == KOS_RES_NORMAL)
      return kos_dyn_with_bind_entered(ctx, node) < 0
          ? EVAL_RAISE() : EVAL_NORMAL();
  }
  if (kr.kind == KOS_RES_RAISE)
    return EVAL_RAISE();

  /* An installed callee has not run, and an awaitable has not settled:
     the completion runs at the settle. */
  KosDynWith_PHASE(node) = KOS_WITH_PHASE_PENDING_ENTER_AWAIT;
  return (kr.kind == KOS_RES_NORMAL) ? EVAL_REPROLOGUE() : kr;
}


/**
   Leave: run __exit__ once. See the extent contract in dynchain.h. The
   body value and a re-raise are not delivered here; the unlink does that
   after the node is gone.
*/
static KosOpResult kos_dyn_with_leave(
    KosContext *ctx,
    KosDynWith *node,
    KosXferEvent ev) {

  (void) ev;

  if (KosDynWith_LEFT(node))
    return EVAL_NORMAL();

  if (KosDynWith_EXIT_PENDING(node) || KosDynWith_ENTER_PENDING(node)) {
    /* A call in flight sat in a frame above this node. The node is
       dyn_top, so that frame is gone (it raised, or the walk crossed
       it): the manager is discharged with nothing to take, and an
       __exit__ that raised has replaced the exception it was handling. */
    kos_dyn_with_abort_impl(node);
    return EVAL_NORMAL();
  }

  if (! KosDynWith_ENTERED(node)) {
    /* never entered: no __exit__. A dyn-with's copy that a transfer
       attached and has not yet entered arrives this way. */
    KosDynWith_PHASE(node) = KOS_WITH_PHASE_LEFT;
    return EVAL_NORMAL();
  }

  PyObject *exit_fn = KosDynWith_EXIT(node);
  KosObligation *ob = KosDynWith_OB(node);

  /* The exception in flight is stored on the node as the obligation __exit__
     is handling: __exit__ may suspend, and the unlink re-raises it. A
     normal exit passes None three times. */
  if (PyErr_Occurred()) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    if (kos_dyn_ob_store_exception((KosDynNode *) node, typ, val, tb) < 0)
      return EVAL_RAISE();
  }

  /* __exit__ runs as a handler of that exception, as CPython runs it
     inside the except block: sys.exc_info(), a bare raise and
     logging.exception in it see the exception. The state displaced is
     given back when __exit__ is done, by any route. */
  if (ob->kind == KOS_OB_EXCEPTION) {
    KosExcState handling;

    KosExc_Init(&handling);
    KosExc_Copy(&handling, &ob->u.exc);
    KosExc_Fetch(KosDynWith_SAVED(node));
    KosDynWith_EXC_SAVED(node) = 1;
    KosExc_Install(&handling);
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
  KosOpResult kr = KosContext_Call(
      ctx, exit_fn, argv, argc, NULL, 0, NULL, NULL);

  /* A value in hand is settled, or awaited onto the block; settled,
     the exit classifies now. */
  if (kr.kind == KOS_RES_RETURN) {
    PyObject *result;

    kr = KosContext_Settle(ctx, kr.val, &result);
    if (kr.kind == KOS_RES_NORMAL) {
      if (kos_dyn_with_classify_exit(ctx, node, result) < 0)
        return EVAL_RAISE();
      return EVAL_NORMAL();
    }
  }

  if (kr.kind == KOS_RES_RAISE) {
    /* __exit__ itself raised; its exception supersedes the one it was
       handling, which the unlink chains in as context. Leave the pending
       phase: the raise walk aborts it. */
    return EVAL_RAISE();
  }

  /* __exit__ has not returned: suspended, or its frame is installed and
     current. The node keeps the triple and stays on the chain. */
  return (kr.kind == KOS_RES_NORMAL) ? EVAL_REPROLOGUE() : kr;
}


/**
   The pending __enter__ or __exit__ returned; its result is the top of
   the live block.
*/
static int kos_dyn_with_settle(KosContext *ctx, KosDynWith *node) {

  /* A pending with node sits above its owning frame, so that frame is
     live while the node is; without it there is no operand block to
     settle from. */
  if (! KosContext_FRAME(ctx)) {
    PyErr_SetString(PyExc_SystemError,
                    "with settle: pending node with no live frame");
    return -1;
  }

  if (KosDynWith_ENTER_PENDING(node))
    return kos_dyn_with_bind_entered(ctx, node);

  if (KosDynWith_EXIT_PENDING(node)) {
    PyObject *result = KosContext_StackPopRef(ctx);
    if (! result)
      return -1;
    return kos_dyn_with_classify_exit(ctx, node, result);
  }
  return 0;
}


/**
   The pending __enter__ or __exit__ died: the manager is discharged
   with nothing to take.
*/
static void kos_dyn_with_abort_impl(KosDynWith *node) {
  /* what was stored stays for the unlink to chain or discard */
  kos_dyn_with_exit_done_exc(node);
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
  Py_VISIT(KosDynWith_NAME(with_node));
  if (KosExc_Traverse(KosDynWith_SAVED(with_node), visit, arg) < 0)
    return -1;
  return kos_dyn_ob_traverse(KosDynWith_OB(with_node), visit, arg);
}


// The end.
