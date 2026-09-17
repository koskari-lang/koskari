# KOS_OP_BUILD_LIST_APPEND

- **Style**: COUNT. Arg = number of values to append.
- **Status**: Implemented. (`koskari/lib/opcode/BUILD_LIST_APPEND.c`)
- **Stack**: `[..., list, v_0, ..., v_{n-1}] → [..., list]`. Append the top n values to the list at depth n, in place; pop the values; leave the list. TypeError when the list slot is not a list. (Widened from the former NOARG append-one; `compile/#list-append` on a list expression emits count 1.)
- **Compiler form:** `compile/#list` dynamic tail run of k; `compile/#list-append` list-expression path (count 1).
