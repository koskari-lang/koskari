# Opcode families
- [3. No-op and compile-time](index.md#3-no-op-and-compile-time) — NOOP, LABEL, POSITION
- [4. Stack](index.md#4-stack) — DUP, POP, UNPACK, UNPACK_STAR, UNPACK_VAR, UNPACK_FAST
- [5. Constants](index.md#5-constants) — CONST, CONST2, CONST3, CONST4
- [5.1 Pair construction and access](index.md#51-pair-construction-and-access) — CAR, CDR, CONS, PROPER, PAIR_SPLICE
- [6. Mode control](index.md#6-mode-control) — AWAIT, BREAK, CONTINUE, RAISE, RETURN, YIELD, YIELD_FROM
- [7. Iteration](index.md#7-iteration) — ITER_NEXT, NEXT
- [8. Jumps](index.md#8-jumps) — JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
- [9. Check jumps](index.md#9-check-jumps) — CHECK_COMPARE, CHECK_COMPARE_VAR, CHECK_COMPARE_FAST, CHECK_EXCEPTION
- [9.1 Switch dispatch](index.md#91-switch-dispatch) — SWITCH_EQ, SWITCH_EQ_CONST, SWITCH_EQ_VAR, SWITCH_EQ_FAST, SWITCH_EQ_VAR_ATTR, SWITCH_EQ_FAST_ATTR
- [10. Introspection](index.md#10-introspection) — GLOBALS, LOCALS, IMPORT, IMPORT_FROM, IMPORT_STAR
- [11. Exception handling](index.md#11-exception-handling) — EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH
- [12. Variables by name](index.md#12-variables-by-name) — VAR, VAR2, VAR3, VAR4, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE, VAR_STORE_CONST, VAR_ATTR*, VAR_ITER, VAR_NEXT, VAR_FORMAT, VAR_FORMAT_CONST
- [13. Fast variables](index.md#13-fast-variables) — FAST, FAST2, FAST3, FAST4, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE, FAST_STORE_CONST, FAST_ATTR*, FAST_ITER, FAST_NEXT, FAST_FORMAT, FAST_FORMAT_CONST
- [14. Attributes](index.md#14-attributes) — ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE
- [15. Items](index.md#15-items) — ITEM, CONST_ITEM, and variants
- [15.1 Reference dereference](index.md#151-reference-dereference) — DEREF
- [16. Argument production](index.md#16-argument-production) — BUILD_ARGS, BUILD_FUNCTION, BUILD_KWDS, etc.
- [17. Calls](index.md#17-calls) — CALL, CALL_NAMES, CALL_STAR, CALL_ATTR*, TAIL_*
- [18. Block control](index.md#18-block-control) — PUSH_FOREACH, PUSH_LOOP, PUSH_NS, PUSH_COLLECT, PUSH_TRY, PUSH_FINALLY, CLEAR_*, POP_*
- [18.1 Context managers (with / dyn-with)](index.md#181-context-managers-with--dyn-with) — PUSH_WITH, POP_WITH, PUSH_DYN_WITH, POP_DYN_WITH
- [19. Object protocol](index.md#19-object-protocol) — OBJECT_* (comparisons, predicates, numeric, general)
- [20. Delimited continuations](index.md#20-delimited-continuations) — PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, POP_PROMPT
- [21. Undelimited continuations (call/cc)](index.md#21-undelimited-continuations-callcc) — PUSH_CC, POP_CC, CAPTURE_CC
- [22. Dynamic wind](index.md#22-dynamic-wind) — PUSH_WIND, POP_WIND
- [23. Parameterize extent](index.md#23-parameterize-extent-and-parameter-reads) — PUSH_PARAM, PARAM, POP_PARAM
- [24. Sentinel](index.md#24-sentinel) — THEEND

### 2.1 Quick-reference index

| Opcode | Style | One-line effect | Section |
|--------|-------|------------------|---------|
| NOOP | NOARG | Advance PC only | [§3](opcodes/kos-op-noop.md) |
| LABEL | COMPILE | Jump target; stripped post-compile | [§3](opcodes/kos-op-label.md) |
| POSITION | COMPILE | Source position; stripped post-compile | [§3](opcodes/kos-op-position.md) |
| DUP | COUNT | Duplicate top N stack elements | [§4](opcodes/kos-op-dup.md) |
| POP | COUNT | Pop N elements | [§4](opcodes/kos-op-pop.md) |
| UNPACK | COUNT | Realize TOS as list; push N scalars (seq[0] is TOS) | [§4](opcodes/kos-op-unpack.md) |
| UNPACK_STAR | EXTRA | Prefix / middle / suffix unpack (two COUNT fields) | [§4](opcodes/kos-op-unpack-star.md) |
| UNPACK_VAR | EXTRA | STORE seq[0], seq[1] into two VAR names | [§4](opcodes/kos-op-unpack-var.md) |
| UNPACK_FAST | EXTRA | STORE seq[0], seq[1] into two fast slots | [§4](opcodes/kos-op-unpack-fast.md) |
| CONST | CONST | Push constant from pool | [§5](opcodes/kos-op-const.md) |
| CONST2 | EXTRA | Push two constants in row order | [§5](opcodes/kos-op-const2.md) |
| CONST3 | EXTRA | Push three constants in row order | [§5](opcodes/kos-op-const3.md) |
| CONST4 | EXTRA | Push four constants in row order | [§5](opcodes/kos-op-const4.md) |
| CAR | COUNT | Apply car **n** times (**n** ≥ 1); **n** = 0 invalid | [§5.1](opcodes/kos-op-car.md) |
| CDR | COUNT | Apply cdr **n** times (**n** ≥ 1); **n** = 0 invalid | [§5.1](opcodes/kos-op-cdr.md) |
| CONS | COUNT | Pop N values; push (e_0 . ... . e_{N-1}) | [§5.1](opcodes/kos-op-cons.md) |
| PROPER | COUNT | Pop N values; push proper list (e_0 ... e_{N-1}) | [§5.1](opcodes/kos-op-proper.md) |
| PAIR_SPLICE | COUNT | Concatenate N pair/tuple segments into one proper list | [§5.1](opcodes/kos-op-pair-splice.md) |
| AWAIT | NOARG | Suspend on awaitable; resume with value | [§6](opcodes/kos-op-await.md) |
| BREAK | COUNT | Jump to loop break target | [§6](opcodes/kos-op-break.md) |
| CONTINUE | COUNT | Jump to loop continue target | [§6](opcodes/kos-op-continue.md) |
| RAISE | COUNT | Pop N, raise exception | [§6](opcodes/kos-op-raise.md) |
| RETURN | COUNT | Pop return value; `EVAL_RETURN(val)` → **`eval_event_return`**; **`KOS_RES_RETURN`** at driver boundary | [§6](opcodes/kos-op-return.md) |
| YIELD | COUNT | Pop value; `EVAL_YIELD(val)` → **`apply_kos_op_result`** → **`eval_event_yield`** (walk/freeze, optional boundary deliver) | [§6](opcodes/kos-op-yield.md) |
| YIELD_FROM | NOARG | Delegate to sub-iterator/gen; PC-stable; send/throw/close | [§6](opcodes/kos-op-yield-from.md) |
| ITER_NEXT | LABEL | Next from region iterator; else jump | [§7](opcodes/kos-op-iter-next.md) |
| OBJECT_NEXT | COUNT | Stack-based next (1 or 2 args) | [§7](opcodes/kos-op-object-next.md) |
| JUMP | LABEL | Unconditional jump to pc | [§8](opcodes/kos-op-jump.md) |
| JUMP_FALSE | LABEL | Jump if TOS false | [§8](opcodes/kos-op-jump-false.md) |
| JUMP_TRUE | LABEL | Jump if TOS true | [§8](opcodes/kos-op-jump-true.md) |
| JUMP_AND | LABEL | Short-circuit AND | [§8](opcodes/kos-op-jump-and.md) |
| JUMP_OR | LABEL | Short-circuit OR | [§8](opcodes/kos-op-jump-or.md) |
| CHECK_COMPARE | EXTRA | Chain-compare N operands under a KosCompare selector; jump if chain fails | [§9](opcodes/kos-op-check-compare.md) |
| CHECK_COMPARE_VAR | EXTRA | Arity-2 fused check; operands from VAR/CONST per CMPSRC bits | [§9](opcodes/kos-op-check-compare-var.md) |
| CHECK_COMPARE_FAST | EXTRA | Arity-2 fused check; operands from FAST/CONST per CMPSRC bits | [§9](opcodes/kos-op-check-compare-fast.md) |
| CHECK_EXCEPTION | LABEL | Jump if exception no match | [§9](opcodes/kos-op-check-exception.md) |
| SWITCH_EQ | LABEL | Equality dispatch; on match pop both and jump; on mismatch pop key only, fall through | [§9.1](opcodes/kos-op-switch-eq.md) |
| SWITCH_EQ_CONST | EXTRA | Compare TOS to constant RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](opcodes/kos-op-switch-eq-const.md) |
| SWITCH_EQ_VAR | EXTRA | Compare TOS to namespace var RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](opcodes/kos-op-switch-eq-var.md) |
| SWITCH_EQ_FAST | EXTRA | Compare TOS to fast-slot RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](opcodes/kos-op-switch-eq-fast.md) |
| SWITCH_EQ_VAR_ATTR | EXTRA | Compare TOS to getattr(var, attr); on match pop TOS and jump; on mismatch fall through | [§9.1](opcodes/kos-op-switch-eq-var-attr.md) |
| SWITCH_EQ_FAST_ATTR | EXTRA | Compare TOS to getattr(fast, attr); on match pop TOS and jump; on mismatch fall through | [§9.1](opcodes/kos-op-switch-eq-fast-attr.md) |
| GLOBALS | NOARG | Push globals dict | [§10](opcodes/kos-op-globals.md) |
| LOCALS | NOARG | Push locals dict | [§10](opcodes/kos-op-locals.md) |
| IMPORT | EXTRA | Import module; bind root or leaf (self-contained) | [§10.1](opcodes/kos-op-import.md) |
| IMPORT_FROM | EXTRA | Import module; bind named attrs (self-contained) | [§10.1](opcodes/kos-op-import-from.md) |
| IMPORT_STAR | EXTRA | Import module; bind its public names | [§10.1](opcodes/kos-op-import-star.md) |
| EXCEPTION | NOARG | Push current exception | [§11](opcodes/kos-op-exception.md) |
| EXCEPTION_CLEAR | NOARG | Clear exception state | [§11](opcodes/kos-op-exception-clear.md) |
| EXCEPTION_MATCH | NOARG | Pop type(s), push match result | [§11](opcodes/kos-op-exception-match.md) |
| VAR | VAR | Load by name | [§12](opcodes/kos-op-var.md) |
| VAR2 | EXTRA | Load two names, push in row order | [§12](opcodes/kos-op-var2.md) |
| VAR3 | EXTRA | Load three names, push in row order | [§12](opcodes/kos-op-var3.md) |
| VAR4 | EXTRA | Load four names, push in row order | [§12](opcodes/kos-op-var4.md) |
| VAR_BIND | VAR | BIND: use TOS ref to resolve name | [§12](opcodes/kos-op-var-bind.md) |
| VAR_STORE | VAR | STORE: write TOS value into name | [§12](opcodes/kos-op-var-store.md) |
| VAR_STORE_CONST | EXTRA | STORE: write constant into name (no stack) | [§12](opcodes/kos-op-var-store-const.md) |
| FAST | FASTVAR | Load from slot | [§13](opcodes/kos-op-fast.md) |
| FAST2 | EXTRA | Load two slots, push in row order | [§13](opcodes/kos-op-fast2.md) |
| FAST3 | EXTRA | Load three slots, push in row order | [§13](opcodes/kos-op-fast3.md) |
| FAST4 | EXTRA | Load four slots, push in row order | [§13](opcodes/kos-op-fast4.md) |
| FAST_BIND | FASTVAR | BIND: use TOS ref to resolve slot | [§13](opcodes/kos-op-fast-bind.md) |
| FAST_STORE | FASTVAR | STORE: write TOS value into slot | [§13](opcodes/kos-op-fast-store.md) |
| FAST_STORE_CONST | EXTRA | STORE: write constant into slot (no stack) | [§13](opcodes/kos-op-fast-store-const.md) |
| ATTR | CONST | Get attribute | [§14](opcodes/kos-op-attr.md) |
| ATTR_STORE | CONST | Set attribute | [§14](opcodes/kos-op-attr-store.md) |
| ITEM | NOARG | Get item (key on stack) | [§15](opcodes/kos-op-item.md) |
| ITEM_STORE | NOARG | Set item | [§15](opcodes/kos-op-item-store.md) |
| CONST_ITEM | CONST | Get item (constant key) | [§15](opcodes/kos-op-const-item.md) |
| DEREF | NOARG | Pop Ref, call ref.deref(), push result | [§15.1](opcodes/kos-op-deref.md) |
| BUILD_FUNCTION | EXTRA | Build function from spec, with keyword defaults and annotations from names tuples | [§16](opcodes/kos-op-build-function.md) |
| CALL | COUNT | Call with N positional args | [§17](opcodes/kos-op-call.md) |
| CALL_NAMES | COUNT | Call with keyword names on stack | [§17](opcodes/kos-op-call-names.md) |
| CALL_STAR | STAR | Call with *args and/or **kwargs | [§17](opcodes/kos-op-call-star.md) |
| CALL_VAR | EXTRA | Call callable from namespace by name | [§17](opcodes/kos-op-call-var.md) |
| CALL_FAST | EXTRA | Call callable from fast local slot | [§17](opcodes/kos-op-call-fast.md) |
| CALL_VAR_NAMES | EXTRA | Call by name with keyword args | [§17](opcodes/kos-op-call-var-names.md) |
| CALL_FAST_NAMES | EXTRA | Call from fast slot with keyword args | [§17](opcodes/kos-op-call-fast-names.md) |
| CALL_VAR_STAR | EXTRA | Call by name with *args/**kwargs | [§17](opcodes/kos-op-call-var-star.md) |
| CALL_FAST_STAR | EXTRA | Call from fast slot with *args/**kwargs | [§17](opcodes/kos-op-call-fast-star.md) |
| CALL_ATTR | EXTRA | Call obj.name with N positional args | [§17](opcodes/kos-op-call-attr.md) |
| CALL_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args | [§17](opcodes/kos-op-call-attr-names.md) |
| CALL_FAST_ATTR | EXTRA | Call obj.name with N positional args; obj from fast slot | [§17](opcodes/kos-op-call-fast-attr.md) |
| CALL_VAR_ATTR | EXTRA | Call obj.name with N positional args; obj from VAR | [§17](opcodes/kos-op-call-var-attr.md) |
| CALL_FAST_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from fast slot | [§17](opcodes/kos-op-call-fast-attr-names.md) |
| CALL_VAR_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from VAR | [§17](opcodes/kos-op-call-var-attr-names.md) |
| CALL_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs | [§17](opcodes/kos-op-call-attr-star.md) |
| CALL_VAR_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from VAR | [§17](opcodes/kos-op-call-var-attr-star.md) |
| CALL_FAST_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from fast slot | [§17](opcodes/kos-op-call-fast-attr-star.md) |
| TAIL_CALL | COUNT | Tail call (replace frame) | [§17](opcodes/kos-op-tail-call.md) |
| TAIL_CALL_NAMES | COUNT | Tail call with keyword names | [§17](opcodes/kos-op-tail-call-names.md) |
| TAIL_CALL_STAR | STAR | Tail call with *args and/or **kwargs | [§17](opcodes/kos-op-tail-call-star.md) |
| TAIL_CALL_ATTR | EXTRA | Tail call obj.name with N positional args | [§17](opcodes/kos-op-tail-call-attr.md) |
| TAIL_CALL_ATTR_NAMES | EXTRA | Tail call obj.name with positional + keyword args | [§17](opcodes/kos-op-tail-call-attr-names.md) |
| TAIL_CALL_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs | [§17](opcodes/kos-op-tail-call-attr-star.md) |
| TAIL_CALL_VAR | EXTRA | Tail call callable from namespace by name | [§17](opcodes/kos-op-tail-call-var.md) |
| TAIL_CALL_VAR_NAMES | EXTRA | Tail call by name with keyword args | [§17](opcodes/kos-op-tail-call-var-names.md) |
| TAIL_CALL_VAR_STAR | EXTRA | Tail call by name with *args/**kwargs | [§17](opcodes/kos-op-tail-call-var-star.md) |
| TAIL_CALL_VAR_ATTR | EXTRA | Tail call obj.name; obj from VAR | [§17](opcodes/kos-op-tail-call-var-attr.md) |
| TAIL_CALL_VAR_ATTR_NAMES | EXTRA | Tail call obj.name with keyword args; obj from VAR | [§17](opcodes/kos-op-tail-call-var-attr-names.md) |
| TAIL_CALL_VAR_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs; obj from VAR | [§17](opcodes/kos-op-tail-call-var-attr-star.md) |
| TAIL_CALL_FAST | EXTRA | Tail call callable from fast local slot | [§17](opcodes/kos-op-tail-call-fast.md) |
| TAIL_CALL_FAST_NAMES | EXTRA | Tail call from fast slot with keyword args | [§17](opcodes/kos-op-tail-call-fast-names.md) |
| TAIL_CALL_FAST_STAR | EXTRA | Tail call from fast slot with *args/**kwargs | [§17](opcodes/kos-op-tail-call-fast-star.md) |
| TAIL_CALL_FAST_ATTR | EXTRA | Tail call obj.name; obj from fast slot | [§17](opcodes/kos-op-tail-call-fast-attr.md) |
| TAIL_CALL_FAST_ATTR_NAMES | EXTRA | Tail call obj.name with keyword args; obj from fast slot | [§17](opcodes/kos-op-tail-call-fast-attr-names.md) |
| TAIL_CALL_FAST_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs; obj from fast slot | [§17](opcodes/kos-op-tail-call-fast-attr-star.md) |
| PUSH_FOREACH | NOARG | Push foreach region; break target from extent table | [§18](opcodes/kos-op-push-foreach.md) |
| PUSH_LOOP | NOARG | Push loop region; break target from extent table | [§18](opcodes/kos-op-push-loop.md) |
| POP_FOREACH | NOARG | Pop foreach region; push loop result | [§18](opcodes/kos-op-pop-foreach.md) |
| POP_LOOP | NOARG | Pop loop region; push loop result | [§18](opcodes/kos-op-pop-loop.md) |
| CLEAR_LOOP | NOARG | Disable top LOOP region (before POP_LOOP) | [§18](opcodes/kos-op-clear-loop.md) |
| CLEAR_FOREACH | NOARG | Disable top FOREACH region (before POP_FOREACH) | [§18](opcodes/kos-op-clear-foreach.md) |
| PUSH_NS | EXTRA | Push namespace scope (nspec + names in extra row) | [§18](opcodes/kos-op-push-ns.md) |
| PUSH_COLLECT | CONST | Push collector namespace scope | [§18](opcodes/kos-op-push-collect.md) |
| POP_COLLECT | NOARG | Pop collect scope, push ns | [§18](opcodes/kos-op-pop-collect.md) |
| BUILD_CLASS | NOARG | Consume pos+ns, call metaclass | [§18](opcodes/kos-op-build-class.md) |
| PUSH_TRY | LABEL | Push try region | [§18](opcodes/kos-op-push-try.md) |
| PUSH_PROMPT | CONST | Push prompt (delimited cont.) | [§20](opcodes/kos-op-push-prompt.md) |
| PUSH_SHIFT | CONST | Capture continuation, enter shift block | [§20](opcodes/kos-op-push-shift.md) |
| POP_SHIFT | NOARG | Return from shift block without invoking k | [§20](opcodes/kos-op-pop-shift.md) |
| POP_PROMPT | NOARG | Pop prompt | [§20](opcodes/kos-op-pop-prompt.md) |
| THEEND | NOARG | Unreachable; no runtime | [§24](opcodes/kos-op-theend.md) |

**Completeness:** Every member of the `KosOpCode` enum in `koskari/lib/opcodes.incl` has a dedicated `### KOS_OP_*` heading in this document (some sections group related opcodes under one narrative first, then list each name). Search for `### KOS_OP_` to enumerate. Opcodes not listed in `opcodes.incl` are
not documented here until they are added to the enum.

