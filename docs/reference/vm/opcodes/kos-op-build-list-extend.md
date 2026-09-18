# KOS_OP_BUILD_LIST_EXTEND

- **Style**: NOARG.
- **Stack**: `[..., list, iterable] → [..., list]`. Extend the list in place from the TOS iterable (`PySequence_InPlaceConcat`, so any iterable the list API accepts); pop the iterable; leave the list. TypeError when the list slot is not a list.
- **Compiler form:** `compile/#list` `*:` splat segment.
