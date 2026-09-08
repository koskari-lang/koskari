/* Included by dynchain.c — xfer_plan */
/* === Transfer plan build and execution === */

/* === LCA computation === */


/**
   Linear search from chain toward root for a stable node_id (graft fallback
   when LCA walk finds no intersection across disjoint templates).
*/
static KosDynNode *kos_dyn_find_by_id(KosDynNode *chain, uint32_t id) {
  KosDynNode *node = chain;
  while (node) {
    if (KosDynNode_NODE_ID(node) == id)
      return node;
    node = KosDynNode_PARENT(node);
  }
  return NULL;
}


/**
   Lowest common ancestor of two chain pointers via transient marks on chain a,
   or kos_dyn_find_by_id(a, graft_point_id) when disjoint. Callers:
   kos_dyn_plan_transfer and other builders that need unwind vs install split.
*/
KosDynNode *kos_dyn_find_lca(KosDynNode *a, KosDynNode *b, uint32_t graft_point_id) {
  if (! a || ! b)
    return NULL;

  KosDynNode *orig_a = a;

  /* Mark every node in chain a with the transient visited flag. */
  for (KosDynNode *n = a; n; n = KosDynNode_PARENT(n))
    KosDynNode_FLAGS(n) |= KOS_DYNF_LCA_VISITED;

  /* Walk chain b; the first marked node is the LCA.  If the chains are
   * disjoint (e.g. severed continuation template), no marked node is found. */
  KosDynNode *lca = NULL;
  for (KosDynNode *n = b; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_FLAGS(n) & KOS_DYNF_LCA_VISITED) {
      lca = n;
      break;
    }
  }

  /* Clear the transient marks from chain a. */
  for (KosDynNode *n = a; n; n = KosDynNode_PARENT(n))
    KosDynNode_FLAGS(n) &= ~KOS_DYNF_LCA_VISITED;

  if (! lca && graft_point_id)
    return kos_dyn_find_by_id(orig_a, graft_point_id);
  return lca;
}


/* === Transfer plan building === */


static KosTransferPlan *xfer_plan_cache = NULL;


/**
   Allocate a transfer plan with action_count action slots. Plan and actions
   are one allocation; default 32 slots, realloc entire block if more needed.
   Reuses cached plan when available. Returns NULL on failure (PyErr_NoMemory).
*/
static KosTransferPlan *kos_transfer_plan_alloc(
    size_t action_count) {

  size_t capacity = (action_count <= KOS_XFER_PLAN_DEFAULT_ACTIONS)
      ? KOS_XFER_PLAN_DEFAULT_ACTIONS
      : action_count;
  size_t size = sizeof(KosTransferPlan) + capacity * sizeof(KosTransferAction);

  KosTransferPlan *plan = NULL;
  if (xfer_plan_cache) {
    plan = xfer_plan_cache;
    xfer_plan_cache = NULL;
    if (KosTransferPlan_ACTION_CAPACITY(plan) < action_count) {
      capacity = action_count;
      size = sizeof(KosTransferPlan) + capacity * sizeof(KosTransferAction);
      plan = PyMem_Realloc(plan, size);
      if (! plan) {
        PyErr_NoMemory();
        return NULL;
      }
    }
  } else {
    plan = PyMem_Malloc(size);
    if (! plan) {
      PyErr_NoMemory();
      return NULL;
    }
  }

  memset(plan, 0, size);
  plan->action_count = action_count;
  plan->action_index = 0;
  plan->action_capacity = capacity;
  return plan;
}


/* Transfer plan counting and filling helpers.  Used by kos_dyn_plan_transfer. */


/**
   True when node contributes an unwind EXTENT_ACTION during plan build.
*/
static inline int xfer_plan_unwind_includes(KosDynNode *node) {

  if (! KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_UNWIND_ACTION))
    return 0;
  if (KosDynNode_IS_WIND(node)) {
    KosDynWind *wind = (KosDynWind *) node;
    return KosDynWind_ENTERED(wind) && KosDynWind_AFTER_THUNK(wind);
  }

  return 1;
}


/**
   Count how many transfer actions are needed to unwind from current to lca.
   Walks the dynamic chain from current inward toward lca.  For each node:
   - WIND with an "after" hook (unwind): one RUN_HOOK action.
   - PARAM or DYN_WITH: one NODE_UNWIND action.
   Used to size the plan's action array before filling it.
*/
static size_t xfer_plan_count_unwind_and_params(
    KosDynNode *current,
    const KosDynNode *lca) {

  size_t n = 0;
  KosDynNode *node = current;
  while (node && node != lca) {
    if (xfer_plan_unwind_includes(node))
      n++;
    node = KosDynNode_PARENT(node);
  }
  return n;
}


/**
   Count how many "before" wind hooks lie between target and lca.
   Walks from target toward lca.  Each WIND node that has a before hook and
   has not yet been entered contributes one RUN_HOOK action when we later
   re-enter the segment.  Used to size the plan and to allocate the temp
   array in xfer_plan_fill_wind_actions.
*/
static size_t xfer_plan_count_wind_before(
    KosDynNode *target,
    const KosDynNode *lca) {

  size_t n = 0;
  KosDynNode *node = target;
  while (node && node != lca) {
    if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (! KosDynWind_ENTERED(wind) && KosDynWind_BEFORE_THUNK(wind))
        n++;
    }
    node = KosDynNode_PARENT(node);
  }
  return n;
}


/**
   Fill plan action slots with unwind-phase steps: RUN_HOOK (after) and
   NODE_UNWIND (PARAM/DYN_WITH), from current inward to lca.  Order is
   inner-to-outer so that when the plan is executed, we run after hooks
   and unwind nodes in the correct order.  Advances *idx past the filled slots.
*/
static void xfer_plan_fill_unwind_actions(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *current,
    const KosDynNode *lca) {

  KosDynNode *node = current;
  while (node && node != lca) {
    if (xfer_plan_unwind_includes(node)) {
      KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_ACTION;
      KosTransferAction_EXTENT_NODE(&KosTransferPlan_ACTIONS(plan)[*idx]) = node;
      KosTransferAction_EXTENT_PHASE(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_PHASE_UNWIND;
      if (KOS_XFER_TRACE)
        SAYF("[xfer] plan action[%zu] = EXTENT_ACTION (unwind) node=%p",
             *idx, (void *) node);
      (*idx)++;
    }
    node = KosDynNode_PARENT(node);
  }
}


/**
   Fill a single INSTALL action: the plan will replace the current dynamic
   chain with a clone of the segment rooted at target.  Advances *idx by one.
*/
static void xfer_plan_fill_install_action(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *target) {

  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_INSTALL;
  KosTransferAction_INSTALL_SEGMENT(&KosTransferPlan_ACTIONS(plan)[*idx]) = target;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan action[%zu] = INSTALL segment=%p",
         *idx, (void *) target);
  (*idx)++;
}


/**
   Fill a single PUSH action: append a clone of the segment to the current
   chain (graft bottom to dyn_top).  Advances *idx by one.
*/
static void xfer_plan_fill_push_action(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *target) {

  KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_PUSH;
  KosTransferAction_INSTALL_SEGMENT(&KosTransferPlan_ACTIONS(plan)[*idx]) = target;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] plan action[%zu] = PUSH segment=%p",
         *idx, (void *) target);
  (*idx)++;
}


/**
   Fill plan action slots with wind "before" hooks from lca toward target.
   We need outer-to-inner order at run time, but the chain walk is
   target-to-lca (inner-to-outer).  So we collect wind nodes into a
   temporary array, then emit RUN_HOOK actions in reverse order.
   Returns 0 on success, -1 on allocation failure.
*/
static int xfer_plan_fill_wind_actions(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *target,
    const KosDynNode *lca,
    size_t wind_count) {

  if (wind_count == 0)
    return 0;

  KosDynWind **wind_nodes = PyMem_Malloc(sizeof(KosDynWind *) * wind_count);
  if (! wind_nodes) {
    /* Ownership inversion: this function frees plan on failure so that callers
       (kos_dyn_plan_transfer, kos_dyn_plan_delimited_push, etc.) can do a
       single unconditional "return -1" without a separate free on this path. */
    PyMem_Free(plan);
    PyErr_NoMemory();
    return -1;
  }

  size_t wi = 0;
  KosDynNode *node = target;
  while (node && node != lca) {
    if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION)) {
      KosDynWind *wind = (KosDynWind *) node;
      if (! KosDynWind_ENTERED(wind) && KosDynWind_BEFORE_THUNK(wind))
        wind_nodes[wi++] = wind;
    }
    node = KosDynNode_PARENT(node);
  }

  while (wi--) {
    KosDynWind *wind = wind_nodes[wi];
    KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_ACTION;
    KosTransferAction_EXTENT_NODE(&KosTransferPlan_ACTIONS(plan)[*idx]) = (KosDynNode *) wind;
    KosTransferAction_EXTENT_PHASE(&KosTransferPlan_ACTIONS(plan)[*idx]) = KOS_XFER_EXTENT_PHASE_WIND;
    (*idx)++;
  }

  PyMem_Free(wind_nodes);
  return 0;
}


/**
   Fill a single INJECT action: the value (or throw) to deliver when the
   transfer completes.  Takes a new reference on value (and exc_type when
   is_throw).  Advances *idx by one.
*/
static void xfer_plan_fill_inject_action(
    KosTransferPlan *plan,
    size_t *idx,
    PyObject *value,
    PyObject *exc_type,
    int is_throw) {

  KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[*idx];
  KosTransferAction_STEP(a) = KOS_XFER_INJECT;
  KosTransferAction_INJECT_VALUE(a) = value;
  Py_XINCREF(value);
  KosTransferAction_INJECT_TYPE(a) = exc_type;
  if (exc_type)
    Py_INCREF(exc_type);
  KosTransferAction_INJECT_IS_THROW(a) = is_throw;
  (*idx)++;
}


static void xfer_plan_fill_truncate_and_inject_action(
    KosTransferPlan *plan,
    size_t *idx,
    KosDynNode *node,
    PyObject *value,
    PyObject *exc_type,
    int is_throw) {

  KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[*idx];
  KosTransferAction_STEP(a) = KOS_XFER_TRUNCATE_AND_INJECT;
  KosTransferAction_TRUNCATE_NODE(a) = node;
  KosTransferAction_TRUNCATE_VALUE(a) = value;
  Py_XINCREF(value);
  KosTransferAction_TRUNCATE_TYPE(a) = exc_type;
  if (exc_type)
    Py_INCREF(exc_type);
  KosTransferAction_TRUNCATE_IS_THROW(a) = is_throw;
  (*idx)++;
}


typedef enum KosXferPlanInjectKind {
  KOS_XFER_PLAN_INJECT_NONE = 0,
  KOS_XFER_PLAN_INJECT,
  KOS_XFER_PLAN_TRUNCATE_AND_INJECT,
} KosXferPlanInjectKind;


typedef struct KosXferPlanOptions {
  KosDynNode *target;
  int unwind_from_current;
  KosDynNode *unwind_stop;
  int segment_op;
  int with_wind;
  KosDynNode *wind_lca;
  KosXferPlanInjectKind inject_kind;
  KosDynNode *truncate_node;
  uint32_t graft_point_id;
} KosXferPlanOptions;


/**
   LCA for wind sizing; optional disjoint fallback or reachability error.
*/
static KosDynNode *kos_dyn_resolve_lca(
    KosDynNode *current,
    KosDynNode *target,
    uint32_t graft_point_id,
    int disjoint_ok,
    int require_reachable) {

  KosDynNode *lca = kos_dyn_find_lca(current, target, graft_point_id);

  if (! lca) {
    KosDynNode *n = current;
    while (n && n != target)
      n = KosDynNode_PARENT(n);
    if (n == target)
      lca = target;
    if (! lca && disjoint_ok)
      lca = target;
    if (! lca && require_reachable) {
      PyErr_SetString(PyExc_RuntimeError,
                      "install_only: target not reachable from current chain");
      return NULL;
    }
  }
  return lca;
}


/**
   Allocate and fill a transfer plan from options.  Returns 0 on success.
*/
static int kos_build_xfer_plan(
    KosEvalState *es,
    const KosXferPlanOptions *options,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  KosDynNode *current = KosEvalState_DYN_TOP(es);
  size_t unwind_count = 0;
  size_t wind_count = 0;
  size_t action_count;
  size_t idx = 0;
  KosTransferPlan *plan;
  PyObject *throw_type;

  if (options->unwind_from_current)
    unwind_count = xfer_plan_count_unwind_and_params(current,
                                                     options->unwind_stop);
  if (options->with_wind)
    wind_count = xfer_plan_count_wind_before(options->target,
                                             options->wind_lca);

  action_count = unwind_count;
  if (options->segment_op == KOS_XFER_INSTALL
      || options->segment_op == KOS_XFER_PUSH)
    action_count += 1;
  action_count += wind_count;
  if (options->inject_kind == KOS_XFER_PLAN_INJECT
      || options->inject_kind == KOS_XFER_PLAN_TRUNCATE_AND_INJECT)
    action_count += 1;

  plan = kos_transfer_plan_alloc(action_count);
  if (! plan)
    return -1;

  KosTransferPlan_ACTION_COUNT(plan) = action_count;
  KosTransferPlan_ACTION_INDEX(plan) = 0;
  KosTransferPlan_ORIGINAL_TARGET(plan) = options->target;
  KosTransferPlan_GRAFT_POINT_ID(plan) = options->graft_point_id;
  KosTransferPlan_RESUME_PC(plan) = (uint32_t) -1;
  KosTransferPlan_RESUME_STACK(plan) = NULL;

  if (options->unwind_from_current)
    xfer_plan_fill_unwind_actions(plan, &idx, current, options->unwind_stop);
  if (options->segment_op == KOS_XFER_INSTALL)
    xfer_plan_fill_install_action(plan, &idx, options->target);
  else if (options->segment_op == KOS_XFER_PUSH)
    xfer_plan_fill_push_action(plan, &idx, options->target);
  if (options->with_wind) {
    if (xfer_plan_fill_wind_actions(plan, &idx, options->target,
                                    options->wind_lca, wind_count) < 0) {
      return -1;
    }
  }

  throw_type = is_throw ? (PyObject *) Py_TYPE(value) : NULL;
  if (options->inject_kind == KOS_XFER_PLAN_INJECT) {
    xfer_plan_fill_inject_action(plan, &idx, value, throw_type, is_throw);
  } else if (options->inject_kind == KOS_XFER_PLAN_TRUNCATE_AND_INJECT) {
    xfer_plan_fill_truncate_and_inject_action(plan, &idx,
                                              options->truncate_node,
                                              value, throw_type, is_throw);
  }

  *out_plan = plan;
  return 0;
}


/**
   Build a transfer plan to move control from the current dynamic chain top
   to the segment rooted at target, then deliver value (or throw it).
   Computes the LCA of current and target, counts all required actions
   (unwind hooks + param restores, one INSTALL, wind before hooks, one
   INJECT), allocates the plan, and fills it in phase order.  Caller runs
   the plan by repeatedly calling kos_run_transfer_step until it returns 1.
   Returns 0 on success, -1 on error (e.g. allocation failure).
*/
int kos_dyn_plan_transfer(
    KosEvalState *es,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosXferPlanOptions options;
  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *lca;

  KOS_TRACE_EVENTS_INC(KOS_TE_TRANSFER_PLAN_BUILD);

  lca = kos_dyn_find_lca(current, target, graft_point_id);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] build_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  options.target = target;
  options.unwind_from_current = 1;
  options.unwind_stop = lca;
  options.segment_op = KOS_XFER_INSTALL;
  options.with_wind = 1;
  options.wind_lca = lca;
  options.inject_kind = KOS_XFER_PLAN_INJECT;
  options.truncate_node = NULL;
  options.graft_point_id = graft_point_id;

  return kos_build_xfer_plan(es, &options, value, is_throw, out_plan);
}


/**
   Build a PUSH plan for delimited continuation invoke.  Segment is the
   continuation's frames (first child of prompt in captured_chain).
*/
int kos_dyn_plan_delimited_push(
    KosEvalState *es,
    KosDynNode *segment,
    PyObject *value,
    int is_throw,
    KosTransferPlan **out_plan) {

  KosXferPlanOptions options;

  if (! segment) {
    PyErr_SetString(PyExc_RuntimeError,
                    "delimited push: empty continuation segment");
    return -1;
  }

  options.target = segment;
  options.unwind_from_current = 0;
  options.unwind_stop = NULL;
  options.segment_op = KOS_XFER_PUSH;
  options.with_wind = 1;
  options.wind_lca = NULL;
  options.inject_kind = KOS_XFER_PLAN_INJECT;
  options.truncate_node = NULL;
  options.graft_point_id = 0;

  return kos_build_xfer_plan(es, &options, value, is_throw, out_plan);
}


/**
   Build a transfer plan that skips unwind (INSTALL + wind + INJECT only).
   Use when the current and target chains are disjoint or when the path
   crosses a PROMPT barrier that kos_dyn_pop_top cannot traverse, such as
   after a nested transfer interrupted and completed an outer plan's wind
   thunk.

   Caller must set RESUME_PC, RESUME_STACK on the plan.
   Returns 0 on success, -1 on error.
*/
int kos_dyn_plan_direct_install(
    KosEvalState *es,
    KosDynNode *target,
    PyObject *value,
    int is_throw,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosXferPlanOptions options;
  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *lca;

  lca = kos_dyn_resolve_lca(current, target, graft_point_id, 1, 0);
  if (KOS_XFER_TRACE)
    SAYF("[xfer] direct_install_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  options.target = target;
  options.unwind_from_current = 0;
  options.unwind_stop = NULL;
  options.segment_op = KOS_XFER_INSTALL;
  options.with_wind = 1;
  options.wind_lca = lca;
  options.inject_kind = KOS_XFER_PLAN_INJECT;
  options.truncate_node = NULL;
  options.graft_point_id = graft_point_id;

  return kos_build_xfer_plan(es, &options, value, is_throw, out_plan);
}


/**
   Attach CC_RAISE sentinel at chain root and build a full transfer plan to
   NULL target so unwind runs teardown then re-raises via sentinel. Callers:
   foreign continuation apply error path.
*/
int kos_dyn_plan_cc_unwind(
    KosEvalState *es,
    PyObject *exc_type,
    PyObject *exc_val,
    KosTransferPlan **out_plan) {

  /* Insert the sentinel at the bottom (root) of the chain so that the
     transfer plan unwinds all existing WITH/finally nodes first and hits the
     KosDynCCRaise re-raise step last.  We walk to the current chain root and
     attach the new node below it. */
  KosDynCCRaise *raise_node = kos_dyn_cc_raise_new(NULL, exc_type, exc_val);
  if (! raise_node) {
    return -1;
  }

  KosDynNode *cur = KosEvalState_DYN_TOP(es);
  if (! cur) {
    /* Empty chain: the sentinel becomes dyn_top directly. */
    KosEvalState_DYN_TOP(es) = (KosDynNode *) raise_node;
  } else {
    /* Walk to the bottom of the chain and attach below it. */
    while (KosDynNode_PARENT(cur))
      cur = KosDynNode_PARENT(cur);
    KosDynNode_PARENT(cur) = (KosDynNode *) raise_node;
  }

  /* Build a plan that unwinds from dyn_top all the way to NULL, running
     WITH/finally teardowns along the way.  kos_dyn_plan_transfer with
     target=NULL treats lca=NULL, so it emits EXTENT_ACTION steps for every
     node that has NEEDS_UNWIND_ACTION (including our sentinel at the bottom),
     then INSTALL(NULL), then INJECT.  When the plan reaches the KosDynCCRaise
     EXTENT_ACTION, cc_raise_unwind sets PyErr and returns -1; eval_chain.c
     special-cases that sentinel step (not generic plan failure) and packages
     it as KOS_RES_RAISE for the parent eval loop. */
  return kos_dyn_plan_transfer(es, NULL, Py_None, 0, 0, out_plan);
}


/**
   Plan: unwind extent actions to prompt, then TRUNCATE_AND_INJECT value at
   prompt (shift result delivery). Callers: POP_SHIFT completion.
*/
int kos_dyn_plan_pop_shift(KosEvalState *es, KosDynPrompt *prompt,
                                 PyObject *value, KosTransferPlan **out_plan) {

  KosXferPlanOptions options;
  KosDynNode *prompt_node = (KosDynNode *) prompt;

  options.target = prompt_node;
  options.unwind_from_current = 1;
  options.unwind_stop = prompt_node;
  options.segment_op = 0;
  options.with_wind = 0;
  options.wind_lca = NULL;
  options.inject_kind = KOS_XFER_PLAN_TRUNCATE_AND_INJECT;
  options.truncate_node = prompt_node;
  options.graft_point_id = 0;

  return kos_build_xfer_plan(es, &options, value, 0, out_plan);
}


/**
   Build a transfer plan with only INSTALL and wind-before hooks (no
   INJECT).  Use when the caller will push a frame and value manually
   after the plan runs.  Skips unwind entirely, so safe when path crosses
   PROMPT.  Returns 0 on success.
*/
int kos_dyn_plan_install_only(
    KosEvalState *es,
    KosDynNode *target,
    uint32_t graft_point_id,
    KosTransferPlan **out_plan) {

  KosXferPlanOptions options;
  KosDynNode *current = KosEvalState_DYN_TOP(es);
  KosDynNode *lca;

  lca = kos_dyn_resolve_lca(current, target, graft_point_id, 0, 1);
  if (! lca)
    return -1;
  if (KOS_XFER_TRACE)
    SAYF("[xfer] install_only_plan current=%p target=%p lca=%p",
         (void *) current, (void *) target, (void *) lca);

  options.target = target;
  options.unwind_from_current = 0;
  options.unwind_stop = NULL;
  options.segment_op = KOS_XFER_INSTALL;
  options.with_wind = 1;
  options.wind_lca = lca;
  options.inject_kind = KOS_XFER_PLAN_INJECT_NONE;
  options.truncate_node = NULL;
  options.graft_point_id = graft_point_id;

  return kos_build_xfer_plan(es, &options, NULL, 0, out_plan);
}


/**
   Release the multi-shot continuation clone held by the plan, if any.
   The clone was used for a re-invocation; we release its resume_stack
   chain.  KosTransferPlan_CONTINUATION_REF holds a strong ref to the
   Python wrapper for the whole plan lifetime, so operand-stack DECREFs
   during release cannot drop the last ref to that object.  Then we
   either return the clone to the continuation freelist or PyMem_Free it.
   Called from kos_free_transfer_plan.
*/
static void xfer_plan_release_invoke_clone(KosTransferPlan *plan) {
  if (! KosTransferPlan_INVOKE_CLONE(plan))
    return;

  KosContinuation *clone = KosTransferPlan_INVOKE_CLONE(plan);
  KosTransferPlan_INVOKE_CLONE(plan) = NULL;
  if (KosContinuation_RESUME_STACK(clone)) {
    KosStack_Release(KosContinuation_RESUME_STACK(clone));
    KosContinuation_RESUME_STACK(clone) = NULL;
  }
  if (KosContinuation_CAPTURED_CHAIN(clone)) {
    kos_dynchain_free(KosContinuation_CAPTURED_CHAIN(clone), NULL);
    KosContinuation_CAPTURED_CHAIN(clone) = NULL;
  }
  Py_XDECREF(KosContinuation_TOKEN_REF(clone));
  kos_continuation_release_struct(clone);
}


/**
   Free a transfer plan and all resources it owns.  DECREFs any remaining
   INJECT values (we may have completed only part of the plan), releases
   the invoke clone (if any) via xfer_plan_release_invoke_clone, releases
   the resume_stack chain, DECREFs the continuation ref.  Caches the plan
   struct if cache is empty; otherwise frees it.  Safe to call with NULL.
*/
void kos_free_transfer_plan(KosTransferPlan *plan) {
  if (! plan)
    return;

  for (size_t i = KosTransferPlan_ACTION_INDEX(plan); i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[i];
    if (KosTransferAction_STEP(a) == KOS_XFER_INJECT) {
      Py_XDECREF(KosTransferAction_INJECT_VALUE(a));
      Py_XDECREF(KosTransferAction_INJECT_TYPE(a));
    } else if (KosTransferAction_STEP(a) == KOS_XFER_TRUNCATE_AND_INJECT) {
      Py_XDECREF(KosTransferAction_TRUNCATE_VALUE(a));
      Py_XDECREF(KosTransferAction_TRUNCATE_TYPE(a));
    }
  }
  xfer_plan_release_invoke_clone(plan);
  if (KosTransferPlan_RESUME_STACK(plan)) {
    KosStack_Release(KosTransferPlan_RESUME_STACK(plan));
    KosTransferPlan_RESUME_STACK(plan) = NULL;
  }
  Py_XDECREF(KosTransferPlan_CONTINUATION_REF(plan));
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  Py_XDECREF(KosTransferPlan_STEP_AWAITABLE(plan));
  KosTransferPlan_STEP_AWAITABLE(plan) = NULL;
  Py_XDECREF(KosTransferPlan_STEP_RESUME_INJECT(plan));
  KosTransferPlan_STEP_RESUME_INJECT(plan) = NULL;

  if (! xfer_plan_cache) {
    size_t cap = KosTransferPlan_ACTION_CAPACITY(plan);
    memset(plan, 0, sizeof(KosTransferPlan) + cap * sizeof(KosTransferAction));
    plan->action_capacity = cap;  /* preserve for reuse */
    xfer_plan_cache = plan;
  } else {
    PyMem_Free(plan);
  }
}


/* Transfer step handlers (one per action type).  Used by kos_run_transfer_step. */


/**
   Map a KosOpResult from an extent contract call or an unlink onto the
   plan step protocol: 0 on NORMAL, -1 on RAISE or FATAL, and
   KOS_DYN_UNWIND_ASYNC_STARTED when the node's leave or enter is in
   flight. An AWAIT's payload is stashed on the plan for the drain to
   hand to the driver; the step resumes at the await revive.
*/
static int xfer_map_result(KosTransferPlan *plan, KosOpResult r) {

  if (r.kind == KOS_RES_AWAIT) {
    Py_XSETREF(KosTransferPlan_STEP_AWAITABLE(plan), r.val);
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  }
  if (r.kind == KOS_RES_REPROLOGUE)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  return (r.kind == KOS_RES_NORMAL) ? 0 : -1;
}


/**
   Walk segment top to bottom; set stop to parent of bottom (clone bound).
*/
KOS_OP_INLINE void kos_chain_segment_bounds(
    KosDynNode *top,
    KosDynNode **bottom_out,
    KosDynNode **stop_out) {

  KosDynNode *bottom = top;

  while (bottom && KosDynNode_PARENT(bottom))
    bottom = KosDynNode_PARENT(bottom);
  if (bottom_out)
    *bottom_out = bottom;
  if (stop_out)
    *stop_out = bottom ? KosDynNode_PARENT(bottom) : NULL;
}


/**
   Transfer resume_stack onto the innermost stack owner and set resume PC.
*/
KOS_OP_INLINE void xfer_plan_apply_resume_state(
    KosEvalState *es,
    KosTransferPlan *plan) {

  if (KosTransferPlan_RESUME_STACK(plan)) {
    KosSegment *new_stack = KosTransferPlan_RESUME_STACK(plan);
    KosDynNode *owner = KosEvalState_InnermostStackOwner(es);
    if (! owner && KosEvalState_FRAME(es))
      owner = (KosDynNode *) KosEvalState_FRAME(es);

    if (owner) {
      KosSegment *old_stack = KosDynNode_STACK(owner);
      KosDynNode_STACK(owner) = new_stack;
      KosTransferPlan_RESUME_STACK(plan) = NULL;

      if (old_stack && old_stack != new_stack)
        KosStack_Release(old_stack);
      kos_dyn_resync_chain(es);
    } else {
      KosStack_Release(new_stack);
      KosTransferPlan_RESUME_STACK(plan) = NULL;
    }
  }

  if (KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1
      && KosEvalState_FRAME(es)) {
    KosEvalState_SET_PC(es, KosTransferPlan_RESUME_PC(plan));
  }
}


/**
   Execute an EXTENT_ACTION step: run extent wind (enter) or unwind
   (exit) for the stored node via kos_dyn_wind or kos_dyn_unwind.
   Returns 0 on sync completion, -1 on error, or
   KOS_DYN_UNWIND_ASYNC_STARTED when async started.
*/
static int xfer_step_extent_action(KosEvalState *es,
                                   KosTransferAction *action) {
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *node = KosTransferAction_EXTENT_NODE(action);
  int phase = KosTransferAction_EXTENT_PHASE(action);
  KosOpResult kr;

  if (KosDynNode_HAS_CAP(node, KOS_DYN_CAP_CONTROL_HOOK)
      && kos_dyn_extent_in_flight(node)) {
    /* In flight for someone else: this plan was built from inside that
       thunk, and its INSTALL replaces the chain under it. Skip. */
    if (KosTransferPlan_PARK_NODE(plan) != node)
      return 0;
    /* This step fired it: resume only once dyn_top is back to what it
       was at the firing, which means the thunk's frame came and went.
       Anything above belongs to the thunk. */
    if (KosEvalState_DYN_TOP(es) != KosTransferPlan_PARK_TOP(plan))
      return KOS_DYN_UNWIND_ASYNC_STARTED;
    if (kos_dyn_extent_settle(es, node) < 0)
      return -1;
    KosTransferPlan_PARK_NODE(plan) = NULL;
  }

  KosTransferPlan_PARK_NODE(plan) = node;
  KosTransferPlan_PARK_TOP(plan) = KosEvalState_DYN_TOP(es);
  if (phase == KOS_XFER_EXTENT_PHASE_WIND)
    kr = kos_dyn_extent_enter(es, node);
  else
    kr = kos_dyn_extent_leave(es, node, KOS_XFER_EVENT_NONE);
  if (kr.kind == KOS_RES_NORMAL)
    KosTransferPlan_PARK_NODE(plan) = NULL;
  return xfer_map_result(plan, kr);
}


#define KOS_STACK_BUF_SIZE 8


/**
   Run install-phase wind for each node with KOS_DYN_CAP_NEEDS_INSTALL_WIND
   along the parent chain from seg toward chain_stop (walk stops before
   chain_stop).  Wind callbacks run in reverse of the collection order
   (outer chain nodes first), matching prior INSTALL/PUSH behavior.

   :param xfer_fail_tag: trace label on wind failure ("INSTALL" or "PUSH")
   :return: 0, -1, or KOS_DYN_UNWIND_ASYNC_STARTED
*/
static int kos_dyn_run_install_wind_span(
    KosEvalState *es,
    KosDynNode *seg,
    const KosDynNode *chain_stop,
    const char *xfer_fail_tag) {

  if (! seg)
    return 0;

  KosDynNode *stack_buf[KOS_STACK_BUF_SIZE];
  KosDynNode **wind_nodes = stack_buf;
  size_t wind_count = 0;
  size_t wind_cap = KOS_STACK_BUF_SIZE;
  KosDynNode *walk = seg;

  while (walk && walk != chain_stop) {
    if (KosDynNode_HAS_CAP(walk, KOS_DYN_CAP_NEEDS_INSTALL_WIND)) {
      if (wind_count == wind_cap) {
        size_t new_cap = wind_cap * 2;
        KosDynNode **next = PyMem_Malloc(new_cap * sizeof(KosDynNode *));
        if (! next) {
          PyErr_NoMemory();
          if (wind_nodes != stack_buf)
            PyMem_Free(wind_nodes);
          return -1;
        }
        memcpy(next, wind_nodes, wind_cap * sizeof(KosDynNode *));
        if (wind_nodes != stack_buf)
          PyMem_Free(wind_nodes);
        wind_nodes = next;
        wind_cap = new_cap;
      }
      wind_nodes[wind_count++] = walk;
    }
    walk = KosDynNode_PARENT(walk);
  }

  if (wind_count == 0)
    return 0;

  /* Run wind in reverse order (outer nodes first) */
  for (size_t i = wind_count; i > 0; i--) {
    int m = xfer_map_result(KosEvalState_PLAN(es),
                            kos_dyn_extent_enter(es, wind_nodes[i - 1]));
    if (m) {
      if (m < 0 && KOS_XFER_TRACE)
        SAYF("[xfer] %s wind failed", xfer_fail_tag);
      if (wind_nodes != stack_buf)
        PyMem_Free(wind_nodes);
      return m;
    }
  }
  if (wind_nodes != stack_buf)
    PyMem_Free(wind_nodes);
  return 0;
}


/**
   After INSTALL clones a template segment onto the live chain, remap
   EXTENT_NODE pointers in remaining plan actions from template nodes to
   their live twins (parallel parent walk from segment tops).
*/
static void xfer_remap_extent_nodes(
    KosTransferPlan *plan,
    KosDynNode *template_top,
    KosDynNode *live_top) {

  size_t idx = KosTransferPlan_ACTION_INDEX(plan) + 1;
  size_t count = KosTransferPlan_ACTION_COUNT(plan);

  while (idx < count) {
    KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[idx];
    if (KosTransferAction_STEP(a) == KOS_XFER_EXTENT_ACTION) {
      KosDynNode *const template_node = KosTransferAction_EXTENT_NODE(a);
      KosDynNode *t = template_top;
      KosDynNode *l = live_top;

      while (t && l) {
        if (t == template_node) {
          KosTransferAction_EXTENT_NODE(a) = l;
          break;
        }
        t = KosDynNode_PARENT(t);
        l = KosDynNode_PARENT(l);
      }
    }
    idx++;
  }
}


/**
   Publish cloned segment on dyn_top.  graft_parent NULL replaces chain;
   non-NULL grafts seg bottom onto graft_parent.  Restores resume state,
   remaps extent nodes, runs install-phase wind.
*/
KOS_OP_INLINE int xfer_step_land_segment(
    KosEvalState *es,
    KosTransferPlan *plan,
    KosDynNode *template_seg,
    KosDynNode *seg,
    KosDynNode *graft_parent,
    const char *xfer_tag) {

  KosDynNode *wind_stop;

  if (! graft_parent) {
    KosDynNode *old_top = KosEvalState_DYN_TOP(es);
    KosEvalState_DYN_TOP(es) = seg;
    kos_dyn_resync_chain(es);
    if (old_top)
      kos_dynchain_free(old_top, NULL);
  } else {
    KosDynNode *seg_bottom = NULL;
    kos_chain_segment_bounds(seg, &seg_bottom, NULL);
    if (seg_bottom)
      KosDynNode_PARENT(seg_bottom) = graft_parent;
    KosEvalState_DYN_TOP(es) = seg;
    kos_dyn_resync_chain(es);
  }

  xfer_plan_apply_resume_state(es, plan);

  if (! graft_parent)
    xfer_remap_extent_nodes(plan, template_seg, seg);

  if (graft_parent)
    wind_stop = graft_parent;
  else
    kos_chain_segment_bounds(seg, NULL, &wind_stop);

  {
    int wind_r = kos_dyn_run_install_wind_span(es, seg, wind_stop, xfer_tag);
    if (wind_r != 0)
      return wind_r;
  }

  if (graft_parent)
    xfer_remap_extent_nodes(plan, template_seg, seg);

  return 0;
}


/**
   Execute an INSTALL step: replace the current dynamic chain with a fresh
   clone of the plan's target segment, restore the frame's stack and PC
   from the plan's resume state, and run param_enter for each PARAM node
   in the new segment (outer to inner).  This "lands" the continuation:
   the context now runs in the captured segment.  On failure we decrement
   the plan's action index so the step can be retried or the caller can
   handle it.  Returns 0 on success, -1 on error,
   KOS_DYN_UNWIND_ASYNC_STARTED when async wind started.
*/
static int xfer_step_install(KosEvalState *es, KosTransferAction *action) {
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *template_seg = KosTransferAction_INSTALL_SEGMENT(action);
  KosDynNode *stop = NULL;
  KosDynNode *seg;

  kos_chain_segment_bounds(template_seg, NULL, &stop);
  seg = kos_dyn_clone_segment(template_seg, stop);
  if (! seg && template_seg) {
    SAY("[xfer] INSTALL clone segment failed");
    KosTransferPlan_ACTION_INDEX(plan)--;
    return -1;
  }
  #if KOS_XFER_TRACE
    SAYF("[xfer] INSTALL segment=%p (fresh) resume_pc=%u (before: dyn_top=%p frame_top=%p)",
         (void *) seg, (unsigned) KosTransferPlan_RESUME_PC(plan),
         (void *) KosEvalState_DYN_TOP(es), (void *) KosEvalState_FRAME(es));
  #endif

  {
    int land_r = xfer_step_land_segment(es, plan, template_seg, seg,
                                        NULL, "INSTALL");
    if (land_r != 0)
      return land_r;
  }

  #if KOS_XFER_TRACE
  if (KosEvalState_FRAME(es)) {
    size_t _install_stack_len = KosEvalState_StackLen(es);
    SAYF("[xfer] INSTALL done: frame_top=%p stack=%p stack_len=%zu resume_pc=%u",
         (void *) KosEvalState_FRAME(es),
         (void *) KosEvalState_StackHead(es),
         _install_stack_len,
         (unsigned) KosTransferPlan_RESUME_PC(plan));
  }
  #endif

  SAYF("[xfer] INSTALL after: dyn_top=%p frame_top=%p",
       (void *) KosEvalState_DYN_TOP(es), (void *) KosEvalState_FRAME(es));
  return 0;
}


/**
   Execute a PUSH step: append a clone of the segment to the current chain,
   grafting the segment bottom to dyn_top.  Does not free the current chain.
   Restores stack/PC from plan and runs param wind for pushed nodes.
   Returns 0 on success, -1 on error,
   KOS_DYN_UNWIND_ASYNC_STARTED when async wind started.
*/
static int xfer_step_push(KosEvalState *es, KosTransferAction *action) {
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *template_seg = KosTransferAction_INSTALL_SEGMENT(action);
  KosDynNode *stop = NULL;
  KosDynNode *seg;
  KosDynNode *current_top;

  kos_chain_segment_bounds(template_seg, NULL, &stop);
  seg = kos_dyn_clone_segment(template_seg, stop);
  if (! seg && template_seg) {
    if (KOS_XFER_TRACE)
      SAY("[xfer] PUSH clone segment failed");
    KosTransferPlan_ACTION_INDEX(plan)--;
    return -1;
  }
  if (KOS_XFER_TRACE)
    SAYF("[xfer] PUSH segment=%p (before: dyn_top=%p frame_top=%p)",
         (void *) seg, (void *) KosEvalState_DYN_TOP(es),
         (void *) KosEvalState_FRAME(es));

  current_top = KosEvalState_DYN_TOP(es);
  {
    int land_r = xfer_step_land_segment(es, plan, template_seg, seg,
                                        current_top, "PUSH");
    if (land_r != 0)
      return land_r;
  }

  if (KOS_XFER_TRACE)
    SAYF("[xfer] PUSH after: dyn_top=%p frame_top=%p",
         (void *) KosEvalState_DYN_TOP(es), (void *) KosEvalState_FRAME(es));
  return 0;
}


/**
   Arm the context's throw-injection slots with val/exc_type (the
   evaluator raises on its next entry).  Borrows both references.
   Shared by xfer_step_inject and xfer_step_truncate_and_inject.
*/
static void xfer_inject_throw(
    KosContext *ctx,
    PyObject *val,
    PyObject *exc_type) {

  KosContext_INJECT_MODE(ctx) = KOS_INJECT_THROW;
  Py_XINCREF(val);
  Py_XDECREF(KosContext_INJECT_VALUE(ctx));
  KosContext_INJECT_VALUE(ctx) = val;
  Py_XDECREF(KosContext_INJECT_TYPE(ctx));
  KosContext_INJECT_TYPE(ctx) = exc_type;
  if (exc_type)
    Py_INCREF(exc_type);
}


/**
   Execute an INJECT step: deliver the continuation's result to the
   resumed context.  If is_throw, set inject mode and store the value for
   the evaluator to raise; otherwise push the value onto the current
   frame's stack.  We consume our reference to the action's value (the
   plan no longer owns it).  Returns 0 on success, -1 on error (e.g.
   PushRef failure).
*/
static int xfer_step_inject(KosEvalState *es, KosTransferAction *action) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  PyObject *val = KosTransferAction_INJECT_VALUE(action);
  PyObject *exc_type = KosTransferAction_INJECT_TYPE(action);
  int is_throw = KosTransferAction_INJECT_IS_THROW(action);

  #if KOS_XFER_TRACE
    size_t _inject_len = KosEvalState_StackLen(es);
    SAYF("[xfer] INJECT frame_top=%p stack=%p stack_len=%zu is_throw=%d",
         (void *) KosEvalState_FRAME(es),
         (void *) KosEvalState_StackHead(es),
         _inject_len, is_throw);
  #endif

  if (is_throw) {
    xfer_inject_throw(ctx, val, exc_type);
    Py_DECREF(val);
  } else {
    if (! KosEvalState_FRAME(es)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "INJECT requires a current frame");
      Py_XDECREF(val);
      KosTransferAction_INJECT_VALUE(action) = NULL;
      return -1;
    }
    if (KosEvalState_StackPushRef(es, val) < 0) {
      Py_XDECREF(val);
      KosTransferAction_INJECT_VALUE(action) = NULL;
      return -1;
    }
    Py_DECREF(val);
  }

  KosTransferAction_INJECT_VALUE(action) = NULL;
  Py_XDECREF(exc_type);
  KosTransferAction_INJECT_TYPE(action) = NULL;
  return 0;
}


/**
   Execute a POP_ONE step: run unwind for current top, unlink and free.
   Supports async (e.g. WITH exit returning awaitable).
*/
static int xfer_step_pop_one(KosEvalState *es) {
  KosDynNode *const top = KosEvalState_DYN_TOP(es);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "POP_ONE: empty chain");
    return -1;
  }
  return xfer_map_result(KosEvalState_PLAN(es),
                         kos_dyn_unlink_top(es, KOS_XFER_EVENT_NONE, 0));
}


/**
   Execute a TRUNCATE_AND_INJECT step: free from current top through the
   given node (inclusive), set dyn_top to node's parent, then inject value.
   If the plan has RESUME_PC set, updates the new top frame's PC.
*/
static int xfer_step_truncate_and_inject(KosEvalState *es,
                                          KosTransferAction *action) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *plan = KosEvalState_PLAN(es);
  KosDynNode *stop_node = KosTransferAction_TRUNCATE_NODE(action);
  KosDynNode *const new_top = stop_node ? KosDynNode_PARENT(stop_node) : NULL;
  KosDynNode *const current = KosEvalState_DYN_TOP(es);

  if (! current) {
    PyErr_SetString(PyExc_RuntimeError, "TRUNCATE_AND_INJECT: empty chain");
    return -1;
  }

  if (KOS_XFER_TRACE)
    SAYF("[xfer] TRUNCATE_AND_INJECT: freeing from current=%p to stop_node=%p (inclusive), new_top will be=%p",
         (void *) current, (void *) stop_node, (void *) new_top);

  while (KosEvalState_DYN_TOP(es) && KosEvalState_DYN_TOP(es) != new_top) {
    int m = xfer_map_result(plan,
                            kos_dyn_unlink_top(es, KOS_XFER_EVENT_NONE, 1));
    if (m)
      return m;
  }
  if (KosEvalState_DYN_TOP(es) != new_top) {
    PyErr_SetString(PyExc_RuntimeError,
                    "TRUNCATE_AND_INJECT: stop node not reachable");
    return -1;
  }

  if (plan && KosTransferPlan_RESUME_PC(plan) != (uint32_t) -1
      && KosEvalState_FRAME(es)) {
    KosEvalState_SET_PC(es, KosTransferPlan_RESUME_PC(plan));
  }

  if (KOS_XFER_TRACE && ! KosTransferAction_TRUNCATE_IS_THROW(action)
      && KosEvalState_FRAME(es)) {
    SAYF("[xfer] TRUNCATE_AND_INJECT: pushing value %R onto frame=%p",
         KosTransferAction_TRUNCATE_VALUE(action),
         (void *) KosEvalState_FRAME(es));
  }
  {
    PyObject *val = KosTransferAction_TRUNCATE_VALUE(action);
    int is_throw = KosTransferAction_TRUNCATE_IS_THROW(action);

    if (is_throw) {
      xfer_inject_throw(ctx, val, KosTransferAction_TRUNCATE_TYPE(action));
      Py_XDECREF(val);
    } else if (KosEvalState_FRAME(es)) {
      if (KosEvalState_StackPushRef(es, val) < 0) {
        Py_DECREF(val);
        KosTransferAction_TRUNCATE_VALUE(action) = NULL;
        return -1;
      }
      Py_DECREF(val);
    } else {
      /* No frame to receive the value; release the plan's reference. */
      Py_XDECREF(val);
    }
  }
  KosTransferAction_TRUNCATE_VALUE(action) = NULL;
  Py_XDECREF(KosTransferAction_TRUNCATE_TYPE(action));
  KosTransferAction_TRUNCATE_TYPE(action) = NULL;
  return 0;
}


/**
   Run the next action in the context's current transfer plan.  If there
   is no plan or the plan is already complete, returns 1 (done).  Otherwise
   advances the action index, dispatches to the appropriate step handler
   (EXTENT_ACTION, INSTALL, INJECT, POP_ONE, TRUNCATE_AND_INJECT), and
   returns: 0 if more steps remain, 1 if the plan is now complete, -1 on
   error, KOS_XFER_SUSPENDED_AWAIT when async.
*/
int kos_run_transfer_step(KosEvalState *es) {

  if (! KosEvalState_PLAN(es))
    return 1;  /* no plan, complete */

  if (KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)) >= KosTransferPlan_ACTION_COUNT(KosEvalState_PLAN(es)))
    return 1;  /* plan complete */

  KosTransferAction *action = &KosTransferPlan_ACTIONS(KosEvalState_PLAN(es))[KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es))];
  if (KOS_XFER_TRACE)
    SAYF("[xfer] step %zu: plan=%p actions=%p step_type=%d (INSTALL=%d)",
         (size_t) KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)),
         (void *) KosEvalState_PLAN(es), (void *) KosTransferPlan_ACTIONS(KosEvalState_PLAN(es)),
         (int) KosTransferAction_STEP(action), (int) KOS_XFER_INSTALL);

  {
    int res = 0;
    switch (KosTransferAction_STEP(action)) {
      case KOS_XFER_EXTENT_ACTION:
        res = xfer_step_extent_action(es, action);
        break;
      case KOS_XFER_INSTALL:
        res = xfer_step_install(es, action);
        break;
      case KOS_XFER_PUSH:
        res = xfer_step_push(es, action);
        break;
      case KOS_XFER_POP_ONE:
        res = xfer_step_pop_one(es);
        break;
      case KOS_XFER_TRUNCATE_AND_INJECT:
        res = xfer_step_truncate_and_inject(es, action);
        break;
      case KOS_XFER_INJECT:
        res = xfer_step_inject(es, action);
        break;
    }
    if (res == KOS_DYN_UNWIND_ASYNC_STARTED)
      return KOS_XFER_SUSPENDED_AWAIT;
    if (res < 0)
      return -1;
  }

  /* Advance only when step completed normally (no early return -1 or
     KOS_XFER_SUSPENDED_AWAIT).  No SyncFromContext here: each xfer_step_*
     leaves es coherent (kos_dyn_resync_chain on INSTALL/PUSH, wind/unwind
     tails on EXTENT, in-place inject or pop paths that already refreshed). */
  KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es))++;
  if (KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)) >= KosTransferPlan_ACTION_COUNT(KosEvalState_PLAN(es)))
    return 1;

  return 0;  /* more steps remain */
}


/**
   Suspend the current transfer plan for a nested transfer.  Pushes the
   active plan onto plan_stack and clears KosEvalState_PLAN(es).

   Called by kos_invoke_continuation when a continuation is invoked while
   a transfer is already in progress (e.g. from a dynamic-wind thunk).
   After the inner transfer completes, run_transfer_plan_to_completion calls
   kos_resume_transfer_plan and rebuilds the remaining steps via
   kos_dyn_plan_direct_install.
*/
void kos_suspend_transfer_plan(KosEvalState *es) {

  if (! KosEvalState_PLAN(es))
    return;

  KosTransferPlan_SUSPENDED(KosEvalState_PLAN(es)) = KosEvalState_PLAN_STACK(es);
  KosEvalState_PLAN_STACK(es) = KosEvalState_PLAN(es);
  KosEvalState_PLAN(es) = NULL;
}


/**
   Resume a suspended transfer plan after a nested transfer completes.  Pops
   from plan_stack and restores it as KosEvalState_PLAN(es) (no-op when
   nothing is suspended).  Pairs with kos_suspend_transfer_plan.
*/
void kos_resume_transfer_plan(KosEvalState *es) {

  if (! KosEvalState_PLAN_STACK(es))
    return;  /* nothing to resume */

  KosEvalState_PLAN(es) = KosEvalState_PLAN_STACK(es);
  KosEvalState_PLAN_STACK(es) = KosEvalState_PLAN(es)->suspended;
  KosEvalState_PLAN(es)->suspended = NULL;
}


/**
   Free all suspended transfer plans on plan_stack.
*/
void kos_free_transfer_plan_stack(KosEvalState *es) {

  while (KosEvalState_PLAN_STACK(es)) {
    KosTransferPlan *p = KosEvalState_PLAN_STACK(es);
    KosEvalState_PLAN_STACK(es) = KosTransferPlan_SUSPENDED(p);
    kos_free_transfer_plan(p);
  }
}


/**
   Disarm the active transfer plan and free any suspended outers on
   plan_stack.  Does not clear PyErr or mutate dyn_top / frame_top / stack.
*/
void kos_abandon_transfer_nest(KosEvalState *es) {

  KosContext *ctx;
  KosTransferPlan *p;

  if (! es)
    return;

  ctx = KosEvalState_CONTEXT(es);
  p = KosEvalState_PLAN(es);
  KosEvalState_PLAN(es) = NULL;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
  kos_free_transfer_plan(p);
  kos_free_transfer_plan_stack(es);

  /* Parked exceptions are not dropped here (they may belong to an
     unrelated in-flight raise); surface any survivors loudly so a stale
     entry can never fail silently. */
  if (KosContext_EXC_STASH(ctx)
      && PyList_GET_SIZE(KosContext_EXC_STASH(ctx)))
    SAYF("[xfer] abandon_transfer_nest: %zd parked exception(s) remain\n",
         PyList_GET_SIZE(KosContext_EXC_STASH(ctx)));
}


/**
   True when plan includes a KOS_XFER_PUSH step (delimited invoke plan).
*/
int kos_xfer_plan_is_delimited_push(const KosTransferPlan *plan) {

  if (! plan)
    return 0;

  for (size_t i = 0; i < KosTransferPlan_ACTION_COUNT(plan); i++) {
    if (KosTransferAction_STEP(&KosTransferPlan_ACTIONS(plan)[i])
        == KOS_XFER_PUSH)
      return 1;
  }

  return 0;
}


/* === Continuation invocation === */


/**
   In-place (Path A) invoke readiness: the capture-time live prompt and
   shift nodes are both still on the current chain (pointer + node_id
   match), the shift node lies above the prompt, no barrier node (foreign
   DELIM, GEN, NEXT, PROMPT, CC_RAISE) sits between dyn_top and the shift
   node, and no transfer plan is in flight. Returns 1 when in-place invoke
   applies, 0 otherwise (fall back to the PUSH path).

   On success, *out_nested_frame is the first FRAME between the SHIFT and
   its prompt (the nested handler's frame), or NULL when the handler
   shares the prompt's frame -- recorded here so the invoke does not
   re-walk the same stretch.
*/
static int kos_delimited_inplace_ready(
    KosEvalState *es,
    KosContinuation *k,
    KosDynFrame **out_nested_frame) {

  KosDynNode *prompt = KosContinuation_LIVE_PROMPT_NODE(k);
  KosDynNode *shift = KosContinuation_LIVE_SHIFT_NODE(k);
  KosDynNode *node;

  if (! prompt || ! shift)
    return 0;
  if (KosEvalState_PLAN(es) || KosEvalState_PLAN_STACK(es))
    return 0;

  node = KosEvalState_DYN_TOP(es);
  while (node && node != shift) {
    switch (KosDynNode_KIND(node)) {
      case KOS_DYN_DELIM:
      case KOS_DYN_GEN:
      case KOS_DYN_NEXT:
      case KOS_DYN_PROMPT:
      case KOS_DYN_CC_RAISE:
        return 0;
      default:
        break;
    }
    node = KosDynNode_PARENT(node);
  }
  if (! node || ! KosDynNode_IS_SHIFT(node)
      || KosDynNode_NODE_ID(node) != KosContinuation_LIVE_SHIFT_NODE_ID(k))
    return 0;

  *out_nested_frame = NULL;
  node = KosDynNode_PARENT(node);
  while (node && node != prompt) {
    /* An INPLACE delim between this SHIFT and the prompt means another
       invoke against the same prompt is already in flight: its span is
       detached and its restitch still expects to find itself directly
       above that prompt. Inserting a second delim there re-parents the
       first, which then fails with "inconsistent INPLACE delim state".
       This is the re-entrant case -- a shift firing inside a continuation
       that is being resumed -- and it belongs on the graft, which
       instantiates the template per invoke rather than mutating the live
       chain. The dyn_top-to-SHIFT walk above already rejects barriers;
       this stretch was unchecked. */
    if (KosDynNode_IS_DELIM(node))
      return 0;
    if (! *out_nested_frame && KosDynNode_IS_FRAME(node))
      *out_nested_frame = (KosDynFrame *) node;
    node = KosDynNode_PARENT(node);
  }
  if (! node || ! KosDynNode_IS_PROMPT(node)
      || KosDynNode_NODE_ID(node) != KosContinuation_LIVE_PROMPT_NODE_ID(k))
    return 0;

  return 1;
}


/**
   In-place (Path A) delimited invoke: detach the invoke span (dyn_top down
   through the live SHIFT node) onto an INPLACE DELIM inserted directly
   above the live prompt, displace the resume frame's stack/pc with the
   captured resume state, and continue on the live chain. No transfer plan,
   no clone, no wind replay; live extents unwind exactly once as the slice
   runs to POP_PROMPT, which restitches via
   kos_dyn_finish_inplace_pop_return. Borrows value.
*/
static int kos_delimited_invoke_inplace(
    KosEvalState *es,
    KosContinuation *k,
    PyObject *value,
    KosDynFrame *nested_frame) {

  KosDynNode *shift = KosContinuation_LIVE_SHIFT_NODE(k);
  KosDynNode *prompt = KosContinuation_LIVE_PROMPT_NODE(k);
  KosDynNode *span_top = KosEvalState_DYN_TOP(es);
  KosDynNode *slice_top = KosDynNode_PARENT(shift);
  KosDynFrame *resume_frame;
  KosSegment *resume_stack;
  KosDynDelim *delim;
  KosDynFrame *return_frame;
  uint32_t return_pc;
  int handler_nested = (nested_frame != NULL);

  /* Resume frame: the FRAME owning the SHIFT, which is what capture
     recorded RESUME_PC and RESUME_STACK against (kos_shift_capture takes
     the first frame walking down from the captured chain's top). Chain
     order root..top is caller FRAME, PROMPT, callee FRAME, SHIFT, so when
     the shift sits in a callee that frame is ABOVE the prompt and a walk
     at-or-below the prompt can never reach it -- installing the callee's
     pc and stack into the caller's frame. The readiness walk records
     that frame (nested_frame); when the shift is lexically inside the
     reset no frame intervenes, so fall back to the frame below the
     prompt. */
  resume_frame = nested_frame;
  if (! resume_frame) {
    KosDynNode *node = prompt;

    while (node && ! KosDynNode_IS_FRAME(node))
      node = KosDynNode_PARENT(node);
    if (! node) {
      PyErr_SetString(PyExc_RuntimeError,
                      "in-place invoke: no frame below prompt");
      return -1;
    }
    resume_frame = (KosDynFrame *) node;
  }

  resume_stack = KosContinuation_RESUME_STACK(k)
      ? KosStack_DeepCopy(KosContinuation_RESUME_STACK(k)) : NULL;
  if (! resume_stack && KosContinuation_RESUME_STACK(k)) {
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }

  KosEvalState_FlushLiveRegsToFrame(es);
  return_frame = KosEvalState_FRAME(es);
  return_pc = return_frame ? KosEvalState_PC(es) : (uint32_t) -1;

  /* Nested handler: the slice returns out of the handler's own frame and
     frees it, so restitch has nothing live to resume into and its final
     "load the invoke-site pc" step -- guarded on the live frame being
     that frame -- silently never fires. Snapshot the handler's
     environment (code, ns, operand block, pc) and resume in the copy. */
  if (handler_nested && return_frame) {
    KosDynFrame *tmpl = kos_dynframe_clone(return_frame);
    if (! tmpl) {
      if (resume_stack)
        KosStack_Release(resume_stack);
      if (! PyErr_Occurred())
        PyErr_NoMemory();
      return -1;
    }
    return_frame = tmpl;
  }

  delim = kos_dyndelim_new(prompt,
                           KOS_DYN_DELIM_MODE_INPLACE,
                           NULL,
                           return_pc,
                           return_frame,
                           NULL);
  if (! delim) {
    if (resume_stack)
      KosStack_Release(resume_stack);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }
  KosDynDelim_RETURN_NODE(delim) = (KosDynNode *) delim;
  if (handler_nested)
    KosDynNode_FLAGS((KosDynNode *) delim) |= KOS_DYNF_HOLDS_RETURN_FRAME;

  /* The invocation re-delimits: a shift firing inside the resumed slice
     must capture up to THIS invocation's iteration node, not the original
     prompt, or its POP_SHIFT discards the remainder of the suspended
     handler (control semantics where shift was promised). The fresh
     instance sits between the delim and the slice; captures inside the
     slice bound at it and its completion routes through the delim. */
  KosDynPrompt *invocation_prompt = kos_dynprompt_new_invocation(
      (KosDynNode *) delim,
      KosDynPrompt_ID(prompt));
  if (! invocation_prompt) {
    KosDynNode_PARENT((KosDynNode *) delim) = NULL;
    kos_dyndelim_free(delim);
    if (resume_stack)
      KosStack_Release(resume_stack);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }

  #if KOS_XFER_TRACE
    SAYF("[xfer] invoke inplace: span_top=%p shift=%p prompt=%p "
         "resume_frame=%p return_pc=%u",
         (void *) span_top, (void *) shift, (void *) prompt,
         (void *) resume_frame, (unsigned) return_pc);
  #endif

  /* Detach the invoke span and stash it on the delim. Its segments'
   * unwind-cache back links die with the detach. */
  KosDynNode_PARENT(shift) = NULL;
  KosDynDelim_RETURN_CHAIN(delim) = span_top;
  kos_dyn_clear_unwind_caches(span_top, NULL);

  /* Wire prompt <- delim <- invocation prompt <- slice so the slice's
   * natural unwind leaves the invocation prompt (then the delim) at
   * dyn_top when POP_PROMPT runs. */
  if (slice_top == prompt) {
    slice_top = (KosDynNode *) invocation_prompt;
  } else {
    KosDynNode *child = slice_top;
    while (KosDynNode_PARENT(child) != prompt)
      child = KosDynNode_PARENT(child);
    KosDynNode_PARENT(child) = (KosDynNode *) invocation_prompt;
    /* The re-spliced slice span's back links likewise predate the new
     * shape. */
    kos_dyn_clear_unwind_caches(slice_top, (KosDynNode *) invocation_prompt);
  }

  /* Displace the resume frame's live state with the captured resume
   * state; the delim owns the displaced stack until restitch. The
   * invoke-site acting pc is saved for extent lookups after restitch
   * (slice execution overwrites the frame's acting pc). */
  /* The displaced segment is stored away from its node until restitch;
     an inline segment cannot leave its node, so detach spills it to a
     heap segment first. */
  {
    KosSegment *displaced = KosStack_Detach(KosDynFrame_STACK(resume_frame));
    if (! displaced && KosDynFrame_STACK(resume_frame)) {
      if (resume_stack)
        KosStack_Release(resume_stack);
      return -1;
    }
    KosDynDelim_SAVED_STACK(delim) = displaced;
    if (displaced)
      KosSegment_BACK(displaced) = NULL;
  }
  KosDynDelim_SAVED_PC(delim) = KosDynFrame_PC(resume_frame);
  KosDynDelim_SAVED_ACTING_PC(delim) = KosEvalState_ACTING_PC(es);
  KosDynFrame_STACK(resume_frame) = resume_stack;
  KosDynFrame_PC(resume_frame) = KosContinuation_RESUME_PC(k);

  KosContinuation_LIVE_SLICE_CONSUMED(k) = 1;

  kos_dyn_commit_dyn_top(es, slice_top);
  kos_dyn_resync_chain(es);
  /* rebuild leaves live PCs alone when the frame is unchanged (invoke
   * frame == resume frame); load them from the displaced frame state. */
  kos_dyn_resync_load_regs(es, resume_frame);

  Py_INCREF(value);
  /* StackPUSH consumes the value reference, on failure as well. */
  return KosEvalState_StackPUSH(es, value);
}


/**
   Invoke k with value or throw. Delimited with the capture-time reset live
   in ancestry: in-place resume (no plan). Otherwise builds and installs a
   transfer plan: PUSH for delimited (caller chain preserved), full plan for
   undelimited. Sets plan resume fields and optional continuation_ref for
   GC. Callers: callable invoke, apply paths.
*/
int kos_invoke_continuation(
    KosEvalState *es,
    KosContinuation *k,
    PyObject *value,
    int is_throw,
    PyObject *continuation_ref) {

  if (! k) {
    PyErr_SetString(PyExc_RuntimeError, "NULL continuation");
    return -1;
  }

  /* In-place (Path A) invoke when the capture-time reset is still live in
     dyn_top ancestry: resume on the live chain, no graft, no wind replay.
     Only the first invoke can run in place (it consumes the live slice);
     re-invokes fall through to the PUSH graft, which instantiates the
     captured template per invoke (multi-shot, e.g. (sum (k 0) (k 1))).
     Throw injection also stays on the PUSH path. */
  if (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED
      && ! is_throw
      && ! KosContinuation_LIVE_SLICE_CONSUMED(k)) {
    KosDynFrame *nested_frame = NULL;

    if (kos_delimited_inplace_ready(es, k, &nested_frame))
      return kos_delimited_invoke_inplace(es, k, value, nested_frame);
  }

  /* For multi-shot, clone the continuation */
  KosContinuation *to_use = k;
  #if KOS_XFER_TRACE
    SAYF("[xfer] kos_invoke_continuation k=%p one_shot=%d captured_chain=%p",
         (void *) k, KosContinuation_ONE_SHOT(k), (void *) KosContinuation_CAPTURED_CHAIN(k));
  #endif
  if (! KosContinuation_ONE_SHOT(k)) {
    to_use = kos_continuation_clone(k);
    #if KOS_XFER_TRACE
      SAYF("[xfer] clone -> to_use=%p", (void *) to_use);
    #endif
    if (! to_use) {
      #if KOS_XFER_TRACE
        SAY("[xfer] clone failed");
      #endif
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError,
                        "continuation clone failed without PyErr");
      return -1;
    }
  }

  /* Delimited k(value): use PUSH so caller stays on chain; install DELIM
   * marker so POP_PROMPT can route back to callsite without context slots. */
  int is_delimited = (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED);
  KosDynNode *push_segment = NULL;
  KosDynDelim *ret_delim = NULL;
  if (is_delimited) {
    KosDynNode *captured = KosContinuation_CAPTURED_CHAIN(to_use);
    /* Segment to push: captured prompt plus body frames/ns nodes. The
       prompt clone already carries its falsified invocation identity,
       assigned once at capture in kos_shift_capture. */
    push_segment = captured;
    ret_delim = kos_dyn_push_delim(
        es,
        KOS_DYN_DELIM_MODE_POP_PROMPT,
        NULL,
        KosEvalState_FRAME(es)
            ? KosEvalState_PC(es) : (uint32_t) -1,
        KosEvalState_FRAME(es),
        NULL);
    if (! ret_delim) {
      if (to_use != k)
        kos_continuation_free(to_use);
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError,
                        "failed to push delimited return marker");
      return -1;
    }
    KosDynDelim_RETURN_NODE(ret_delim) = (KosDynNode *) ret_delim;
    if (KOS_XFER_TRACE)
      SAYF("[xfer] invoke: push DELIM dyn_top=%p kind=%s frame_top=%p return_node=%p",
           (void *) KosEvalState_DYN_TOP(es),
           KosEvalState_DYN_TOP(es)
               ? kos_dynnode_kind_name(KosDynNode_KIND(
                     KosEvalState_DYN_TOP(es)))
               : "NULL",
           (void *) KosEvalState_FRAME(es),
           (void *) KosDynDelim_RETURN_NODE(ret_delim));
  }

  /* Build plan: PUSH for delimited; full transfer for undelimited. */
  KosTransferPlan *plan = NULL;
  int res;
  if (is_delimited) {
    #if KOS_XFER_TRACE
      SAYF("[xfer] kos_dyn_plan_delimited_push (delimited) segment=%p",
           (void *) push_segment);
    #endif
    res = kos_dyn_plan_delimited_push(es, push_segment, value, is_throw,
                                        &plan);
  } else {
    #if KOS_XFER_TRACE
      SAYF("[xfer] kos_dyn_plan_transfer current=%p target=%p (template)",
           (void *) KosEvalState_DYN_TOP(es), (void *) to_use->captured_chain);
    #endif
    res = kos_dyn_plan_transfer(es, to_use->captured_chain,
                                  value, is_throw,
                                  KosContinuation_GRAFT_POINT_ID(to_use),
                                  &plan);
  }
  #if KOS_XFER_TRACE
    SAYF("[xfer] plan build -> res=%d plan=%p", res, (void *) plan);
  #endif
  if (res < 0) {
    if (ret_delim && kos_dyn_pop_delim(es, ret_delim) < 0)
      return -1;
    if (to_use != k)
      kos_continuation_free(to_use);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation plan build failed without PyErr");
    return -1;
  }

  KosTransferPlan_GRAFT_POINT_ID(plan) = KosContinuation_GRAFT_POINT_ID(to_use);
  KosTransferPlan_RESUME_PC(plan) = KosContinuation_RESUME_PC(k);
  /* resume_stack is an alias of a template-owned block, and the PUSH
     step lands a fresh clone of the whole template -- the landed segment
     already carries this invoke's one copy of those operands, in the
     same innermost-owner position apply_resume_state would target. A
     plan-side copy here would only replace identical content, so the
     delimited graft carries none. Undelimited INSTALL keeps the plan
     copy (released in kos_free_transfer_plan if the plan dies early). */
  KosTransferPlan_RESUME_STACK(plan) = (! is_delimited && to_use->resume_stack)
      ? KosStack_DeepCopy(to_use->resume_stack) : NULL;
  #if KOS_XFER_TRACE
    SAYF("[xfer] plan resume_stack=%p",
         (void *) KosTransferPlan_RESUME_STACK(plan));
  #endif

  if (kos_xfer_plan_is_delimited_push(KosEvalState_PLAN(es))) {
    kos_free_transfer_plan(plan);
    if (ret_delim && kos_dyn_pop_delim(es, ret_delim) < 0)
      return -1;
    if (to_use != k)
      kos_continuation_free(to_use);
    kos_abandon_transfer_nest(es);
    PyErr_SetString(PyExc_RuntimeError,
                    "nested continuation invoke during an active delimited"
                    " transfer is not supported");
    return -1;
  }

  /* If a transfer is already in progress (e.g. we were called from a wind
     thunk during an EXTENT_ACTION step), suspend the outer plan so it can be
     replanned and resumed after this inner transfer completes. */
  kos_suspend_transfer_plan(es);
  KosEvalState_ArmTransferPlan(es, plan);
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  if (continuation_ref) {
    Py_INCREF(continuation_ref);
    KosTransferPlan_CONTINUATION_REF(plan) = continuation_ref;
  }

  if (to_use != k) {
    KosTransferPlan_INVOKE_CLONE(plan) = to_use;
    /* Do not release to_use->resume_stack here; that would DECREF the
     * continuation and free k before the plan runs. kos_free_transfer_plan
     * releases it via KosStack_Release then frees to_use.
     * The clone retains ownership of captured_chain until
     * xfer_plan_release_invoke_clone frees it. */
  }

  return 0;
}
