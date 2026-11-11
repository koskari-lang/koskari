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
  Included by dynchain.c. The KOS_DYN_WIND node: a dynamic-wind extent
  with its before and after thunks.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynWind implementation === */


/**
   Allocate dynamic-wind node; caps derived from which thunks are present.
   Callers: kos_dyn_push_wind.
*/
KosDynWind *kos_dynwind_new(
    KosDynNode *parent,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynnode_alloc();
  if (! wind)
    return NULL;

  kos_dynnode_init(wind, KOS_DYN_WIND, parent,
                   KOS_DYN_CAP_CONTROL_HOOK | KOS_DYN_CAP_WINDS);
  KosDynWind_BEFORE_THUNK(wind) = before_thunk;
  if (before_thunk)
    Py_INCREF(before_thunk);
  KosDynWind_AFTER_THUNK(wind) = after_thunk;
  if (after_thunk)
    Py_INCREF(after_thunk);
  KosDynWind_OB(wind)->kind = KOS_OB_NONE;
  KosDynWind_ENTERED(wind) = 0;
  KosDynWind_PHASE(wind) = KOS_WIND_PHASE_IDLE;
  return wind;
}


/**
   Clone thunks and phase for continuation segments.
*/
KosDynWind *kos_dynwind_clone(KosDynWind *wind) {
  if (! wind)
    return NULL;

  /* A thunk in flight, or a value, exception or return stored for its
     completion, belongs to this node's one run and to no template. */
  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN
      || KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_AFTER
      || KosDynWind_OB(wind)->kind != KOS_OB_NONE) {
    kos_raise_prompt_unreachable(
        NULL, KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN
                  ? "before" : "after",
        "cannot capture a continuation from inside a dyn-wind whose %s"
        " thunk is in flight");
    return NULL;
  }

  KosDynWind *clone = kos_dynnode_clone_base(wind);
  if (! clone)
    return NULL;

  KosDynWind_BEFORE_THUNK(clone) = KosDynWind_BEFORE_THUNK(wind);
  if (KosDynWind_BEFORE_THUNK(clone))
    Py_INCREF(KosDynWind_BEFORE_THUNK(clone));
  KosDynWind_AFTER_THUNK(clone) = KosDynWind_AFTER_THUNK(wind);
  if (KosDynWind_AFTER_THUNK(clone))
    Py_INCREF(KosDynWind_AFTER_THUNK(clone));
  KosDynWind_OB(clone)->kind = KOS_OB_NONE;
  KosDynWind_ENTERED(clone) = 0;
  KosDynWind_PHASE(clone) = KOS_WIND_PHASE_IDLE;
  return clone;
}


/**
   DECREF thunks and body_result; return to freelist.
*/
void kos_dynwind_free(KosDynWind *wind) {
  if (! wind)
    return;

  Py_XDECREF(KosDynWind_BEFORE_THUNK(wind));
  KosDynWind_BEFORE_THUNK(wind) = NULL;
  Py_XDECREF(KosDynWind_AFTER_THUNK(wind));
  KosDynWind_AFTER_THUNK(wind) = NULL;
  kos_dyn_ob_clear(KosDynWind_OB(wind));
  kos_dynnode_return_to_freelist(wind);
}


static int dynnode_to_dict_wind(KosDynNode *node, PyObject *data) {

  if (snapshot_set_int(data, "entered", KosDynWind_ENTERED((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "phase", (int) KosDynWind_PHASE((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "before_thunk",
      KosDynWind_BEFORE_THUNK((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "after_thunk",
      KosDynWind_AFTER_THUNK((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "obligation",
      (int) KosDynWind_OB((KosDynWind *) node)->kind) < 0)
    return -1;
  return 0;
}


/**
   Fire thunk with no arguments and classify what happened. The phase
   has already been set to the pending value by the caller.

   :return: EVAL_RAISE; EVAL_AWAIT with the awaitable; EVAL_REPROLOGUE
     when the thunk's frame is installed; EVAL_NORMAL when the thunk ran
     to completion in hand, its result already dropped
*/
static KosOpResult kos_dyn_wind_fire(KosContext *ctx, PyObject *thunk) {
  KosOpResult wr = KosContext_Call(
      ctx, thunk, NULL, 0, NULL, 0, NULL, NULL);

  /* A value in hand is settled, or awaited onto the block; settled, it
     is not the wind's business. */
  if (wr.kind == KOS_RES_RETURN) {
    PyObject *val;

    wr = KosContext_Settle(ctx, wr.val, &val);
    if (wr.kind == KOS_RES_NORMAL) {
      Py_DECREF(val);
      return EVAL_NORMAL();
    }
  }

  if (wr.kind == KOS_RES_RAISE || wr.kind == KOS_RES_AWAIT)
    return wr;
  return EVAL_REPROLOGUE();
}


/**
   Enter: run the before-thunk once. See the extent contract in
   dynchain.h.
*/
static KosOpResult kos_dyn_wind_enter(KosContext *ctx, KosDynWind *node) {

  if (KosDynWind_ENTERED(node))
    return EVAL_NORMAL();
  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN)
    return EVAL_REPROLOGUE();  /* before-thunk in flight */

  if (! KosDynWind_BEFORE_THUNK(node)) {
    KosDynWind_ENTERED(node) = 1;
    return EVAL_NORMAL();
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_BEGIN;
  {
    KosOpResult wr = kos_dyn_wind_fire(ctx, KosDynWind_BEFORE_THUNK(node));
    if (wr.kind != KOS_RES_NORMAL)
      return wr;
  }
  KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_BEGIN;
  KosDynWind_ENTERED(node) = 1;
  return EVAL_NORMAL();
}


/**
   Leave: run the after-thunk once. See the extent contract in
   dynchain.h. The body result is not delivered here; the unlink does
   that after the node is gone.
*/
static KosOpResult kos_dyn_wind_leave(
    KosContext *ctx,
    KosDynWind *node,
    KosXferEvent ev) {

  (void) ev;

  switch (KosDynWind_PHASE(node)) {
    case KOS_WIND_PHASE_POST_AFTER:
      return EVAL_NORMAL();

    case KOS_WIND_PHASE_PRE_AFTER:
    case KOS_WIND_PHASE_PRE_BEGIN:
      /* A thunk in flight sat in a frame above this node. The node is
         dyn_top, so that frame is gone (it raised, or the walk crossed
         it) and the thunk is dead: nothing to wait for and, for a
         before-thunk, never entered. */
      KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
      return EVAL_NORMAL();

    default:
      break;
  }

  if (! KosDynWind_ENTERED(node) || ! KosDynWind_AFTER_THUNK(node)) {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
    return EVAL_NORMAL();
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_AFTER;
  {
    KosOpResult wr = kos_dyn_wind_fire(ctx, KosDynWind_AFTER_THUNK(node));
    if (wr.kind != KOS_RES_NORMAL)
      return wr;
  }
  KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
  return EVAL_NORMAL();
}


/**
   The pending thunk returned; its result is the top of the live block.
*/
static int kos_dyn_wind_settle(KosContext *ctx, KosDynWind *node) {

  if (KosDynWind_PHASE(node) != KOS_WIND_PHASE_PRE_BEGIN
      && KosDynWind_PHASE(node) != KOS_WIND_PHASE_PRE_AFTER)
    return 0;

  if (KosContext_BLOCK(ctx) && *KosContext_BLOCK(ctx)
      && KosSegment_LEN(*KosContext_BLOCK(ctx))) {
    PyObject *val = KosContext_StackPopRef(ctx);
    if (! val)
      return -1;
    Py_DECREF(val);
  }

  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN) {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_BEGIN;
    KosDynWind_ENTERED(node) = 1;
  } else {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
  }
  return 0;
}


/**
   The pending thunk died; nothing to take.
*/
static void kos_dyn_wind_abort(KosDynWind *node) {

  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN
      || KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_AFTER)
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
}


/**
   Allocate and link wind as DYN_TOP (caller runs wind enter separately in
   some paths). Callers: opcode PUSH_WIND.
*/
KosDynWind *kos_dyn_push_wind(
    KosContext *ctx,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynwind_new(KosContext_DYN_TOP(ctx),
                                     before_thunk, after_thunk);
  if (! wind)
    return NULL;

  if (kos_dyn_push(ctx, (KosDynNode *) wind) < 0) {
    kos_dynwind_free(wind);
    return NULL;
  }
  return wind;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_WIND: visit the before and after thunk callables,
   and the stored body result.
*/
static int dynnode_traverse_wind(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWind *wind = (KosDynWind *) node;
  Py_VISIT(KosDynWind_BEFORE_THUNK(wind));
  Py_VISIT(KosDynWind_AFTER_THUNK(wind));
  return kos_dyn_ob_traverse(KosDynWind_OB(wind), visit, arg);
}


// The end.
