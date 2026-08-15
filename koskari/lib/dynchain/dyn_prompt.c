/* Included by dynchain.c — dyn_prompt */
/* === KosDynPrompt implementation === */


/**
   Allocate PROMPT delimiter owning the delimited region's operand-stack
   block. The region's operands live in that block, so dropping the node
   leaves the parent block at the depth it had on entry, and capture takes
   the region's operands without needing to know where they start.
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
  KosDynPrompt_POP_PC(prompt) = (uint32_t) -1;
  kos_dynnode_use_inline_segment((KosDynNode *) prompt);
  return prompt;
}


/**
   Allocate a fresh invocation-prompt instance for an in-place delimited
   invoke: the delimiter the resumed slice runs under, per Danvy/Filinski
   (k = lambda x. <E[x]> -- every invocation re-delimits). Owns NO stack
   block: the slice's operands live in the displaced resume frame below,
   and the marker only provides the prompt instance that captures inside
   the slice bound at and POP_SHIFT resolves to. No pop_pc either --
   completion routes through the paired DELIM (KOS_DYNF_INVOCATION),
   never through a POP_PROMPT resume. Freshly minted node_id.
   Callers: kos_delimited_invoke_inplace.
*/
static KosDynPrompt *kos_dynprompt_new_invocation(
    KosDynNode *parent,
    uint32_t prompt_id) {

  KosDynPrompt *prompt = kos_dynnode_alloc();
  if (! prompt)
    return NULL;

  kos_dynnode_init(prompt, KOS_DYN_PROMPT, parent,
                   KOS_DYN_CAP_BARRIER
                   | KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynNode_FLAGS((KosDynNode *) prompt) |= KOS_DYNF_INVOCATION;
  KosDynPrompt_ID(prompt) = prompt_id;
  KosDynPrompt_POP_PC(prompt) = (uint32_t) -1;
  return prompt;
}


/**
   Clone prompt id for captured segments.
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
  KosDynPrompt_POP_PC(clone) = KosDynPrompt_POP_PC(prompt);
  return clone;
}


/**
   Return node to freelist.
*/
void kos_dynprompt_free(KosDynPrompt *prompt) {
  if (! prompt)
    return;

  kos_dynnode_return_to_freelist((KosDynNode *) prompt);
}


static int dynnode_to_dict_prompt(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "prompt_id",
                        KosDynPrompt_ID((KosDynPrompt *) node)) < 0)
    return -1;
  return 0;
}


/**
   Push prompt via kos_dyn_push. Callers: opcode PUSH_PROMPT.
*/
KosDynPrompt *kos_dyn_push_prompt(
    KosEvalState *es,
    uint32_t prompt_id,
    uint32_t pop_pc) {

  // CONTEXT: dirty, touches context directly
  KosDynPrompt *prompt = kos_dynprompt_new(KosEvalState_DYN_TOP(es), prompt_id);
  if (! prompt)
    return NULL;

  KosDynPrompt_POP_PC(prompt) = pop_pc;

  if (kos_dyn_push(es, (KosDynNode *) prompt) < 0) {
    kos_dynprompt_free(prompt);
    return NULL;
  }

  return prompt;
}


/**
   Pop prompt via barrier-crossing policy API using shared uninstall machinery.
*/
int kos_dyn_pop_prompt(KosEvalState *es, KosDynPrompt *prompt) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) prompt)
    return 0;
  KosOpResult kr = kos_dyn_pop_top_allow_barrier(es, KOS_XFER_EVENT_NONE);
  if (kr.kind == KOS_RES_AWAIT) {
    Py_XDECREF(kr.val);
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  }
  if (kr.kind == KOS_RES_REPROLOGUE)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (kr.kind != KOS_RES_NORMAL)
    return -1;
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_PROMPT: the owned region block.
*/
static int dynnode_traverse_prompt(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}

