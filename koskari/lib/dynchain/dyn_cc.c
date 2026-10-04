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
  Included by dynchain.c. The KOS_DYN_CC node: the call/cc extent
  marker that CAPTURE_CC captures up to.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynCC implementation === */


/**
   Allocate call/cc marker with resume PC after CAPTURE_CC. Callers:
   opcode PUSH_CC.
*/
static KosDynCC *kos_dyncc_new(KosDynNode *parent, uint32_t resume_pc) {

  KosDynCC *cc = kos_dynnode_alloc();
  if (! cc)
    return NULL;

  kos_dynnode_init(cc, KOS_DYN_CC, parent, 0);
  KosDynCC_RESUME_PC(cc) = resume_pc;
  return cc;
}


/**
   Clone resume_pc for continuation templates.
*/
static KosDynCC *kos_dyncc_clone(KosDynCC *cc) {
  if (! cc)
    return NULL;

  KosDynCC *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_CC, NULL, KosDynNode_CAPS(cc));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(cc) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = kos_dynnode_id(cc);
  KosDynCC_RESUME_PC(clone) = KosDynCC_RESUME_PC(cc);
  return clone;
}


/**
   No refs on CC payload; return to freelist.
*/
static void kos_dyncc_free(KosDynCC *cc) {
  if (! cc)
    return;

  kos_dynnode_return_to_freelist(cc);
}


/**
   Link CC as DYN_TOP (minimal push). Callers: PUSH_CC.
*/
KosDynCC *kos_dyn_push_cc(KosEvalState *es, uint32_t resume_pc) {

  // CONTEXT: dirty, touches context directly
  KosDynCC *cc = kos_dyncc_new(KosEvalState_DYN_TOP(es), resume_pc);
  if (! cc)
    return NULL;

  if (kos_dyn_push(es, (KosDynNode *) cc) < 0) {
    kos_dyncc_free(cc);
    return NULL;
  }
  return cc;
}


/**
   Unlink CC from top when it matches; free node. Callers: POP_CC fast path.
*/
int kos_dyn_pop_cc(KosEvalState *es, KosDynCC *cc) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) cc) {
    PyErr_SetString(PyExc_RuntimeError, "CC node is not at top of chain");
    return -1;
  }

  KOS_DYN_POP_REQUIRE_KIND(cc, KOS_DYN_CC, "POP_CC", "CC");
  {
    /* CC is a transfer bookmark: it never owns a segment, so there is
       nothing to deliver -- a bare unlink suffices. */
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) cc);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) cc) = NULL;
    kos_dyncc_free(cc);
  }
  return 0;
}


static int dynnode_to_dict_cc(KosDynNode *node, PyObject *data) {

  return snapshot_set_uint(data, "resume_pc",
                           KosDynCC_RESUME_PC((KosDynCC *) node));
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_CC: no PyObject refs on the node.
*/
static int dynnode_traverse_cc(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


// The end.
