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
  with its before and after asides.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynWind implementation === */


/**
   Allocate dynamic-wind node over the code and row of its PUSH_WIND.
   Callers: kos_dyn_push_wind.
*/
KosDynWind *kos_dynwind_new(
    KosDynNode *parent,
    KosCode *code,
    const KosExtra *row) {

  KosDynWind *wind = kos_dynnode_alloc();
  if (! wind)
    return NULL;

  kos_dynnode_init(wind, KOS_DYN_WIND, parent,
                   KOS_DYN_CAP_CONTROL_HOOK | KOS_DYN_CAP_WINDS);
  Py_XINCREF(code);
  KosDynWind_CODE(wind) = code;  /* owned */
  KosDynWind_ROW(wind) = row;
  KosDynWind_OB(wind)->kind = KOS_OB_NONE;
  KosDynWind_ENTERED(wind) = 0;
  KosDynWind_PHASE(wind) = KOS_WIND_PHASE_IDLE;
  return wind;
}


/**
   Whether a wind can be part of a continuation's template. A segment
   in flight, or a value, exception or return stored for its
   completion, belongs to this node's one run and to no template.
   Returns 0, or -1 with the refusal set.
*/
static int kos_dynwind_capturable(KosDynWind *wind) {

  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN
      || KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_AFTER
      || KosDynWind_OB(wind)->kind != KOS_OB_NONE) {
    kos_raise_prompt_unreachable(
        NULL, KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN
                  ? "before" : "after",
        "cannot capture a continuation from inside a dyn-wind whose %s"
        " segment is in flight");
    return -1;
  }
  return 0;
}


/**
   Clone the code, row and phase for continuation segments.
*/
KosDynWind *kos_dynwind_clone(KosDynWind *wind) {
  if (! wind)
    return NULL;

  if (kos_dynwind_capturable(wind) < 0)
    return NULL;

  KosDynWind *clone = kos_dynnode_clone_base(wind);
  if (! clone)
    return NULL;

  Py_XINCREF(KosDynWind_CODE(wind));
  KosDynWind_CODE(clone) = KosDynWind_CODE(wind);  /* owned */
  KosDynWind_ROW(clone) = KosDynWind_ROW(wind);
  KosDynWind_OB(clone)->kind = KOS_OB_NONE;
  KosDynWind_ENTERED(clone) = 0;
  KosDynWind_PHASE(clone) = KOS_WIND_PHASE_IDLE;
  return clone;
}


/**
   DECREF the code and body_result; return to freelist.
*/
void kos_dynwind_free(KosDynWind *wind) {
  if (! wind)
    return;

  Py_CLEAR(KosDynWind_CODE(wind));
  KosDynWind_ROW(wind) = NULL;
  kos_dyn_ob_clear(KosDynWind_OB(wind));
  kos_dynnode_return_to_freelist(wind);
}


static int dynnode_to_dict_wind(KosDynNode *node, PyObject *data) {

  if (snapshot_set_int(data, "entered", KosDynWind_ENTERED((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "phase", (int) KosDynWind_PHASE((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "code",
      (PyObject *) KosDynWind_CODE((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "obligation",
      (int) KosDynWind_OB((KosDynWind *) node)->kind) < 0)
    return -1;
  return 0;
}


/**
   Run segment, an aside of the wind's code, in a frame above the wind,
   which is dyn_top, over the namespace live at its PUSH_WIND. The
   phase has already been set to the pending value by the caller; the
   segment's return settles it.

   :return: EVAL_REPROLOGUE with the segment's frame installed, or
     EVAL_RAISE
*/
static KosOpResult kos_dyn_wind_fire(
    KosContext *ctx,
    KosDynWind *node,
    const KosExtentDesc *segment) {

  if (! kos_dyn_push_aside(ctx, KosDynWind_CODE(node), KosDynNode_NS(node),
                           segment))
    return EVAL_RAISE();
  return EVAL_REPROLOGUE();
}


/**
   Enter: run the before segment once. See the extent contract in
   dynchain.h.
*/
static KosOpResult kos_dyn_wind_enter(KosContext *ctx, KosDynWind *node) {

  if (KosDynWind_ENTERED(node))
    return EVAL_NORMAL();
  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN)
    return EVAL_REPROLOGUE();  /* before segment in flight */

  if (! KosDynWind_BEFORE(node)) {
    KosDynWind_ENTERED(node) = 1;
    return EVAL_NORMAL();
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_BEGIN;
  return kos_dyn_wind_fire(ctx, node, KosDynWind_BEFORE(node));
}


/**
   Leave: run the after segment once. See the extent contract in
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
      /* A segment in flight sat in a frame above this node. The node
         is dyn_top, so that frame is gone (it raised, or the walk
         crossed it) and the segment is dead: nothing to wait for and,
         for a before segment, never entered. */
      KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
      return EVAL_NORMAL();

    default:
      break;
  }

  if (! KosDynWind_ENTERED(node) || ! KosDynWind_AFTER(node)) {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
    return EVAL_NORMAL();
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_AFTER;
  return kos_dyn_wind_fire(ctx, node, KosDynWind_AFTER(node));
}


/**
   The pending segment returned; its result is the top of the live
   block.
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
   The pending segment died; nothing to take.
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
    KosCode *code,
    const KosExtra *row) {

  KosDynWind *wind = kos_dynwind_new(KosContext_DYN_TOP(ctx), code, row);
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
   GC traverse a KOS_DYN_WIND: visit the code and the stored body
   result.
*/
static int dynnode_traverse_wind(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWind *wind = (KosDynWind *) node;
  Py_VISIT(KosDynWind_CODE(wind));
  return kos_dyn_ob_traverse(KosDynWind_OB(wind), visit, arg);
}


// The end.
