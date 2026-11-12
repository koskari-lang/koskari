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
  Included by dynchain.c. The KOS_DYN_PROMPT node: the reset delimiter.
  The frame called on it, the reset's body or a shift's handler, ends
  it when it returns.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynPrompt implementation === */


/**
   Allocate a PROMPT delimiter. It owns no operands: the region's are
   in the frames above it, the reset's body first among them.
   Callers: kos_dyn_push_prompt.
*/
KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, PyObject *tag) {

  KosDynPrompt *prompt = kos_dynnode_alloc();
  if (! prompt)
    return NULL;

  kos_dynnode_init(prompt, KOS_DYN_PROMPT, parent,
                   KOS_DYN_CAP_BARRIER);
  KosDynPrompt_TAG(prompt) = Py_XNewRef(tag);
  return prompt;
}


/**
   Clone a prompt for captured segments: the same tag, by a reference
   of its own.
*/
static KosDynPrompt *kos_dynprompt_clone(KosDynPrompt *prompt) {
  if (! prompt)
    return NULL;

  KosDynPrompt *clone = kos_dynnode_clone_base(prompt);
  if (! clone)
    return NULL;

  KosDynPrompt_TAG(clone) = Py_XNewRef(KosDynPrompt_TAG(prompt));
  return clone;
}


/**
   Release the tag and return the node to the freelist.
*/
void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  Py_CLEAR(KosDynPrompt_TAG(prompt));
  kos_dynnode_return_to_freelist((KosDynNode *) prompt);
}


static int dynnode_to_dict_prompt(KosDynNode *node, PyObject *data) {

  return snapshot_set_obj(data, "tag",
                          KosDynPrompt_TAG((KosDynPrompt *) node));
}


/**
   Push prompt via kos_dyn_push. Callers: opcode RESET.
*/
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, PyObject *tag) {

  // CONTEXT: dirty, touches context directly
  KosDynPrompt *prompt = kos_dynprompt_new(KosContext_DYN_TOP(ctx), tag);
  if (! prompt)
    return NULL;

  if (kos_dyn_push(ctx, (KosDynNode *) prompt) < 0) {
    kos_dynprompt_free(prompt);
    return NULL;
  }

  return prompt;
}


/**
   Pop prompt via barrier-crossing policy API using shared uninstall machinery.
*/
int kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt) {
  KosOpResult kr;

  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt)
    return 0;

  /* a prompt's leave is synchronous: NORMAL or an error */
  kr = kos_dyn_unlink_top(ctx, KOS_XFER_EVENT_NONE, 1);
  if (kr.kind == KOS_RES_NORMAL)
    return 0;
  Py_XDECREF(kr.val);
  if (! PyErr_Occurred())
    PyErr_SetString(PyExc_SystemError,
                    "kos_dyn_pop_prompt: leave did not complete");
  return -1;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_PROMPT: the tag. A prompt owns no segment.
*/
static int dynnode_traverse_prompt(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynPrompt_TAG(node));
  return 0;
}


// The end.
