#!/usr/bin/env bash
# Grep gates for node-owned operand stack (see design/node-owned-operand-stack.md).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
bad=0
if rg -n 'KosDynFrame_STACK' koskari/lib/opcode >/dev/null 2>&1; then
  echo "check_stack_invariants: KosDynFrame_STACK in koskari/lib/opcode" >&2
  bad=1
fi
if rg -n 'KosStackBlock_(PushBlock|DropBlock|PopBlock)' koskari/lib --glob '!koskari/lib/stack.c' --glob '!koskari/lib/stack.h' >/dev/null 2>&1; then
  echo "check_stack_invariants: PushBlock/DropBlock/PopBlock outside stack.c/stack.h" >&2
  bad=1
fi
exit "$bad"
