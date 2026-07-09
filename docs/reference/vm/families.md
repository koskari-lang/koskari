# Opcode families
- [3. No-op and compile-time](#3-no-op-and-compile-time) — NOOP, LABEL, POSITION
- [4. Stack](#4-stack) — DUP, POP, UNPACK, UNPACK_STAR, UNPACK_VAR, UNPACK_FAST
- [5. Constants](#5-constants) — CONST
- [5.1 Pair construction and access](#51-pair-construction-and-access) — CAR, CDR, CONS, PROPER, PAIR_SPLICE
- [6. Mode control](#6-mode-control) — AWAIT, BREAK, CONTINUE, RAISE, RETURN, YIELD, YIELD_FROM
- [7. Iteration](#7-iteration) — ITER_NEXT, NEXT
- [8. Jumps](#8-jumps) — JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
- [9. Check jumps](#9-check-jumps) — CHECK_EQ, CHECK_GE, CHECK_GT, CHECK_LE, CHECK_LT, CHECK_NE, CHECK_IN, CHECK_NOT_IN, CHECK_IS, CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS, CHECK_EXCEPTION
- [9.1 Switch dispatch](#91-switch-dispatch) — SWITCH_EQ, SWITCH_EQ_CONST, SWITCH_EQ_VAR, SWITCH_EQ_FAST, SWITCH_EQ_VAR_ATTR, SWITCH_EQ_FAST_ATTR
- [10. Introspection](#10-introspection) — GLOBALS, LOCALS, IMPORT, IMPORT_FROM, IMPORT_STAR
- [11. Exception handling](#11-exception-handling) — EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH
- [12. Variables by name](#12-variables-by-name) — VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE, VAR_STORE_CONST, VAR_ATTR*
- [13. Fast variables](#13-fast-variables) — FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE, FAST_STORE_CONST, FAST_ATTR*
- [14. Attributes](#14-attributes) — ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE
- [15. Items](#15-items) — ITEM, CONST_ITEM, and variants
- [15.1 Reference dereference](#151-reference-dereference) — DEREF
- [16. Argument production](#16-argument-production) — BUILD_ARGS, BUILD_FUNCTION, BUILD_KWDS, etc.
- [17. Calls](#17-calls) — CALL, CALL_NAMES, CALL_STAR, CALL_ATTR*, TAIL_*
- [18. Block control](#18-block-control) — PUSH_FOREACH, PUSH_LOOP, PUSH_NS, PUSH_COLLECT, PUSH_TRY, PUSH_FINALLY, CLEAR_*, POP_*
- [18.1 Context managers (with / dyn-with)](#181-context-managers-with--dyn-with) — PUSH_WITH, POP_WITH, PUSH_DYN_WITH, POP_DYN_WITH
- [19. Object protocol](#19-object-protocol) — OBJECT_* (comparisons, predicates, numeric, general)
- [20. Delimited continuations](#20-delimited-continuations) — PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, POP_PROMPT, INVOKE_K, INVOKE_K_VAR
- [21. Undelimited continuations (call/cc)](#21-undelimited-continuations-callcc) — PUSH_CC, POP_CC, CAPTURE_CC
- [22. Dynamic wind](#22-dynamic-wind) — PUSH_WIND, POP_WIND
- [23. Parameterize extent](#23-parameterize-extent) — PUSH_PARAM, PARAM, POP_PARAM
- [24. Sentinel](#24-sentinel) — THEEND

### 2.1 Quick-reference index

| Opcode | Style | One-line effect | Section |
|--------|-------|------------------|---------|
| NOOP | NOARG | Advance PC only | [§3](#kos_op_noop) |
| LABEL | COMPILE | Jump target; stripped post-compile | [§3](#kos_op_label) |
| POSITION | COMPILE | Source position; stripped post-compile | [§3](#kos_op_position) |
| DUP | COUNT | Duplicate top N stack elements | [§4](#kos_op_dup) |
| POP | COUNT | Pop N elements | [§4](#kos_op_pop) |
| UNPACK | COUNT | Realize TOS as list; push N scalars (seq[0] is TOS) | [§4](#kos_op_unpack) |
| UNPACK_STAR | EXTRA | Prefix / middle / suffix unpack (two COUNT fields) | [§4](#kos_op_unpack_star) |
| UNPACK_VAR | EXTRA | STORE seq[0], seq[1] into two VAR names | [§4](#kos_op_unpack_var) |
| UNPACK_FAST | EXTRA | STORE seq[0], seq[1] into two fast slots | [§4](#kos_op_unpack_fast) |
| CONST | CONST | Push constant from pool | [§5](#kos_op_const) |
| CAR | COUNT | Apply car **n** times (**n** ≥ 1); **n** = 0 invalid | [§5.1](#kos_op_car) |
| CDR | COUNT | Apply cdr **n** times (**n** ≥ 1); **n** = 0 invalid | [§5.1](#kos_op_cdr) |
| CONS | COUNT | Pop N values; push (e_0 . ... . e_{N-1}) | [§5.1](#kos_op_cons) |
| PROPER | COUNT | Pop N values; push proper list (e_0 ... e_{N-1}) | [§5.1](#kos_op_proper) |
| PAIR_SPLICE | COUNT | Concatenate N pair/tuple segments into one proper list | [§5.1](#kos_op_pair_splice) |
| AWAIT | NOARG | Suspend on awaitable; resume with value | [§6](#kos_op_await) |
| BREAK | COUNT | Jump to loop break target | [§6](#kos_op_break) |
| CONTINUE | COUNT | Jump to loop continue target | [§6](#kos_op_continue) |
| RAISE | COUNT | Pop N, raise exception | [§6](#kos_op_raise) |
| RETURN | COUNT | Pop return value; `EVAL_RETURN(val)` → **`eval_event_return`**; **`KOS_RES_RETURN`** at driver boundary | [§6](#kos_op_return) |
| YIELD | COUNT | Pop value; `EVAL_YIELD(val)` → **`apply_kos_op_result`** → **`eval_event_yield`** (walk/freeze, optional boundary deliver) | [§6](#kos_op_yield) |
| YIELD_FROM | NOARG | Delegate to sub-iterator/gen; PC-stable; send/throw/close | [§6](#kos_op_yield_from) |
| ITER_NEXT | LABEL | Next from region iterator; else jump | [§7](#kos_op_iter_next) |
| NEXT | COUNT | Stack-based next (1 or 2 args) | [§7](#kos_op_next) |
| JUMP | LABEL | Unconditional jump to pc | [§8](#kos_op_jump) |
| JUMP_FALSE | LABEL | Jump if TOS false | [§8](#kos_op_jump_false) |
| JUMP_TRUE | LABEL | Jump if TOS true | [§8](#kos_op_jump_true) |
| JUMP_AND | LABEL | Short-circuit AND | [§8](#kos_op_jump_and) |
| JUMP_OR | LABEL | Short-circuit OR | [§8](#kos_op_jump_or) |
| CHECK_EQ | EXTRA | Chain-compare N operands with `==`; jump if chain fails | [§9](#kos_op_check_eq) |
| CHECK_NE | EXTRA | Chain-compare N operands with `!=`; jump if chain fails | [§9](#kos_op_check_ne) |
| CHECK_LT | EXTRA | Chain-compare N operands with `<`; jump if chain fails | [§9](#kos_op_check_lt) |
| CHECK_LE | EXTRA | Chain-compare N operands with `<=`; jump if chain fails | [§9](#kos_op_check_le) |
| CHECK_GT | EXTRA | Chain-compare N operands with `>`; jump if chain fails | [§9](#kos_op_check_gt) |
| CHECK_GE | EXTRA | Chain-compare N operands with `>=`; jump if chain fails | [§9](#kos_op_check_ge) |
| CHECK_IN | LABEL | Jump if value not in container | [§9](#kos_op_check_in) |
| CHECK_NOT_IN | LABEL | Jump if value in container | [§9](#kos_op_check_not_in) |
| CHECK_IS | LABEL | Jump if left is not right | [§9](#kos_op_check_is) |
| CHECK_IS_NOT | LABEL | Jump if left is right | [§9](#kos_op_check_is_not) |
| CHECK_EXCEPTION | LABEL | Jump if exception no match | [§9](#kos_op_check_exception) |
| SWITCH_EQ | LABEL | Equality dispatch; on match pop both and jump; on mismatch pop key only, fall through | [§9.1](#kos_op_switch_eq) |
| SWITCH_EQ_CONST | EXTRA | Compare TOS to constant RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_const) |
| SWITCH_EQ_VAR | EXTRA | Compare TOS to namespace var RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_var) |
| SWITCH_EQ_FAST | EXTRA | Compare TOS to fast-slot RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_fast) |
| SWITCH_EQ_VAR_ATTR | EXTRA | Compare TOS to getattr(var, attr); on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_var_attr) |
| SWITCH_EQ_FAST_ATTR | EXTRA | Compare TOS to getattr(fast, attr); on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_fast_attr) |
| GLOBALS | NOARG | Push globals dict | [§10](#kos_op_globals) |
| LOCALS | NOARG | Push locals dict | [§10](#kos_op_locals) |
| IMPORT | EXTRA | Import module; bind root or leaf (self-contained) | [§10.1](#kos_op_import) |
| IMPORT_FROM | EXTRA | Import module; bind named attrs (self-contained) | [§10.1](#kos_op_import_from) |
| IMPORT_STAR | EXTRA | Import module; bind its public names | [§10.1](#kos_op_import_star) |
| EXCEPTION | NOARG | Push current exception | [§11](#kos_op_exception) |
| EXCEPTION_CLEAR | NOARG | Clear exception state | [§11](#kos_op_exception_clear) |
| EXCEPTION_MATCH | NOARG | Pop type(s), push match result | [§11](#kos_op_exception_match) |
| VAR | VAR | Load by name | [§12](#kos_op_var) |
| VAR_BIND | VAR | BIND: use TOS ref to resolve name | [§12](#kos_op_var_bind) |
| VAR_STORE | VAR | STORE: write TOS value into name | [§12](#kos_op_var_store) |
| VAR_STORE_CONST | EXTRA | STORE: write constant into name (no stack) | [§12](#kos_op_var_store_const) |
| FAST | FASTVAR | Load from slot | [§13](#kos_op_fast) |
| FAST_BIND | FASTVAR | BIND: use TOS ref to resolve slot | [§13](#kos_op_fast_bind) |
| FAST_STORE | FASTVAR | STORE: write TOS value into slot | [§13](#kos_op_fast_store) |
| FAST_STORE_CONST | EXTRA | STORE: write constant into slot (no stack) | [§13](#kos_op_fast_store_const) |
| ATTR | CONST | Get attribute | [§14](#kos_op_attr) |
| ATTR_STORE | CONST | Set attribute | [§14](#kos_op_attr_store) |
| ITEM | NOARG | Get item (key on stack) | [§15](#kos_op_item) |
| ITEM_STORE | NOARG | Set item | [§15](#kos_op_item_store) |
| CONST_ITEM | CONST | Get item (constant key) | [§15](#kos_op_const_item) |
| DEREF | NOARG | Pop Ref, call ref.deref(), push result | [§15.1](#kos_op_deref) |
| BUILD_FUNCTION | EXTRA | Build function from spec, with keyword defaults and annotations from names tuples | [§16](#kos_op_build_function) |
| CALL | COUNT | Call with N positional args | [§17](#kos_op_call) |
| CALL_NAMES | COUNT | Call with keyword names on stack | [§17](#kos_op_call_names) |
| CALL_STAR | STAR | Call with *args and/or **kwargs | [§17](#kos_op_call_star) |
| CALL_VAR | EXTRA | Call callable from namespace by name | [§17](#kos_op_call_var) |
| CALL_FAST | EXTRA | Call callable from fast local slot | [§17](#kos_op_call_fast) |
| CALL_VAR_NAMES | EXTRA | Call by name with keyword args | [§17](#kos_op_call_var_names) |
| CALL_FAST_NAMES | EXTRA | Call from fast slot with keyword args | [§17](#kos_op_call_fast_names) |
| CALL_VAR_STAR | EXTRA | Call by name with *args/**kwargs | [§17](#kos_op_call_var_star) |
| CALL_FAST_STAR | EXTRA | Call from fast slot with *args/**kwargs | [§17](#kos_op_call_fast_star) |
| CALL_ATTR | EXTRA | Call obj.name with N positional args | [§17](#kos_op_call_attr) |
| CALL_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args | [§17](#kos_op_call_attr_names) |
| CALL_FAST_ATTR | EXTRA | Call obj.name with N positional args; obj from fast slot | [§17](#kos_op_call_fast_attr) |
| CALL_VAR_ATTR | EXTRA | Call obj.name with N positional args; obj from VAR | [§17](#kos_op_call_var_attr) |
| CALL_FAST_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from fast slot | [§17](#kos_op_call_fast_attr_names) |
| CALL_VAR_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from VAR | [§17](#kos_op_call_var_attr_names) |
| CALL_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs | [§17](#kos_op_call_attr_star) |
| CALL_VAR_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from VAR | [§17](#kos_op_call_var_attr_star) |
| CALL_FAST_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from fast slot | [§17](#kos_op_call_fast_attr_star) |
| TAIL_CALL | COUNT | Tail call (replace frame) | [§17](#kos_op_tail_call) |
| TAIL_CALL_NAMES | COUNT | Tail call with keyword names | [§17](#kos_op_tail_call_names) |
| TAIL_CALL_STAR | STAR | Tail call with *args and/or **kwargs | [§17](#kos_op_tail_call_star) |
| PUSH_FOREACH | NOARG | Push foreach region; break target from extent table | [§18](#kos_op_push_foreach) |
| PUSH_LOOP | NOARG | Push loop region; break target from extent table | [§18](#kos_op_push_loop) |
| POP_FOREACH | NOARG | Pop foreach region; push loop result | [§18](#kos_op_pop_foreach) |
| POP_LOOP | NOARG | Pop loop region; push loop result | [§18](#kos_op_pop_loop) |
| CLEAR_LOOP | NOARG | Disable top LOOP region (before POP_LOOP) | [§18](#kos_op_clear_loop) |
| CLEAR_FOREACH | NOARG | Disable top FOREACH region (before POP_FOREACH) | [§18](#kos_op_clear_foreach) |
| PUSH_NS | EXTRA | Push namespace scope (nspec + names in extra row) | [§18](#kos_op_push_ns) |
| PUSH_COLLECT | CONST | Push collector namespace scope | [§18](#kos_op_push_collect) |
| POP_COLLECT | NOARG | Pop collect scope, push ns | [§18](#kos_op_pop_collect) |
| BUILD_CLASS | NOARG | Consume pos+ns, call metaclass | [§18](#kos_op_build_class) |
| PUSH_TRY | LABEL | Push try region | [§18](#kos_op_push_try) |
| PUSH_PROMPT | CONST | Push prompt (delimited cont.) | [§20](#kos_op_push_prompt) |
| PUSH_SHIFT | CONST | Capture continuation, enter shift block | [§20](#kos_op_push_shift) |
| POP_SHIFT | NOARG | Return from shift block without invoking k | [§20](#kos_op_pop_shift) |
| POP_PROMPT | NOARG | Pop prompt | [§20](#kos_op_pop_prompt) |
| INVOKE_K | NOARG | Pop value, pop k; invoke k(value) | [§20](#kos_op_invoke_k) |
| INVOKE_K_VAR | CONST | Invoke continuation by name | [§20](#kos_op_invoke_k_var) |
| THEEND | NOARG | Unreachable; no runtime | [§24](#kos_op_theend) |

**Completeness:** Every member of the `KosOpCode` enum in `koskari/lib/opcodes.incl` has a dedicated `### KOS_OP_*` heading in this document (some sections group related opcodes under one narrative first, then list each name). Search for `### KOS_OP_` to enumerate. Opcodes not listed in `opcodes.incl` are
not documented here until they are added to the enum.

