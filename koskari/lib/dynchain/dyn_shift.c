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
  Included by dynchain.c. The KOS_DYN_SHIFT node: the shift handler
  bookmark.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynShift implementation === */


/**
   Marker for shift handler extent (no frame). Callers: kos_dyn_push_shift.
*/
static KosDynShift *kos_dynshift_new(
    KosDynNode *parent,
    uint32_t prompt_node_id) {
  KosDynShift *shift = (KosDynShift *) kos_dynnode_alloc();
  if (! shift)
    return NULL;

  kos_dynnode_init((KosDynNode *) shift, KOS_DYN_SHIFT, parent, 0);
  KosDynShift_PROMPT_NODE_ID(shift) = prompt_node_id;
  return shift;
}


/**
   Clone for captured chain segments.
*/
static KosDynShift *kos_dynshift_clone(KosDynShift *shift) {
  if (! shift)
    return NULL;

  KosDynShift *clone = (KosDynShift *) kos_dynnode_clone_base(
      (KosDynNode *) shift);
  if (! clone)
    return NULL;

  KosDynShift_PROMPT_NODE_ID(clone) = KosDynShift_PROMPT_NODE_ID(shift);
  return clone;
}


/**
   Return shift node to freelist.
*/
static void kos_dynshift_free(KosDynShift *shift) {
  if (! shift)
    return;

  kos_dynnode_return_to_freelist(shift);
}


/**
   Push SHIFT node via kos_dyn_push. Callers: opcode PUSH_SHIFT.
*/
int kos_dyn_push_shift(KosEvalState *es, uint32_t prompt_node_id) {
  // CONTEXT: dirty, touches context directly
  KosDynShift *shift = kos_dynshift_new(KosEvalState_DYN_TOP(es),
                                        prompt_node_id);
  if (! shift)
    return -1;

  return kos_dyn_push(es, (KosDynNode *) shift);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_SHIFT: no PyObject refs on the node.
*/
static int dynnode_traverse_shift(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


static int dynnode_to_dict_shift(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "prompt_node_id",
                        KosDynShift_PROMPT_NODE_ID(node)) < 0)
    return -1;
  return 0;
}


// The end.
