/* Included by dynchain.c — dyn_prompt */
/* === KosDynPrompt implementation === */


/**
   Allocate PROMPT delimiter (barrier only; operand stack stays on the frame).
   Callers: kos_dyn_push_prompt.
*/
KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynnode_alloc();
  if (! prompt)
    return NULL;

  kos_dynnode_init(prompt, KOS_DYN_PROMPT, parent,
                   KOS_DYN_CAP_BARRIER
                   | KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynPrompt_ID(prompt) = prompt_id;
  KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;
  return prompt;
}


/**
   Clone prompt id for captured segments; pending shift wrapper not cloned.
*/
static KosDynPrompt *kos_dynprompt_clone(KosDynPrompt *prompt) {
  if (! prompt)
    return NULL;

  KosDynPrompt *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_PROMPT, NULL, KosDynNode_CAPS(prompt));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(prompt) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(prompt);
  KosDynPrompt_ID(clone) = KosDynPrompt_ID(prompt);
  KosDynPrompt_PENDING_SHIFT_WRAPPER(clone) = NULL;
  if (KosDynNode_STACK((KosDynNode *) prompt)) {
    KosDynNode_STACK((KosDynNode *) clone) =
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) prompt));
    if (! KosDynNode_STACK((KosDynNode *) clone)) {
      kos_dynprompt_free(clone);
      return NULL;
    }
  }
  return clone;
}


/**
   DECREF pending wrapper; return node to freelist.
*/
void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  Py_XDECREF(KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt));
  KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;
  kos_dynnode_release_stack((KosDynNode *) prompt);
  kos_dynnode_return_to_freelist((KosDynNode *) prompt);
}


/**
   Push prompt via kos_dyn_push. Callers: opcode PUSH_PROMPT.
*/
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynprompt_new(KosContext_DYN_TOP(ctx), prompt_id);
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
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
    return 0;
  }

  return kos_dyn_pop_top_allow_barrier(ctx, KOS_XFER_EVENT_NONE, NULL);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_PROMPT: visit pending shift wrapper if any.
*/
static int dynnode_traverse_prompt(KosDynNode *node, visitproc visit, void *arg) {
  int tr = kos_stackblock_traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynPrompt_PENDING_SHIFT_WRAPPER((KosDynPrompt *) node));
  return 0;
}

