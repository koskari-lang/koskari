#!/usr/bin/env python3
"""
Fail if product C sources call kos_dyn_* with KosContext * as the first
argument. Mutating dynchain entry points must use kos_dyn_*_es(KosEvalState *)
outside the dynchain implementation.

Allowlist: read-only helpers and kos_dyn_gen_frozen_needs_install(eval_ctx, ...).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "koskari" / "lib"

ALLOW_FIRST_ARG_CTX = frozenset(
    {
        "kos_dyn_find_next_above_frame",
        "kos_dyn_find_awaiting_foreach",
        "kos_dyn_find_yield_from_iter_boundary",
        "kos_dyn_need_kos_agen_boundary_deliver",
        "kos_dyn_gen_frozen_needs_install",
    }
)

LINE_RE = re.compile(
    r"\b(kos_dyn_\w+)\s*\(\s*(ctx|eval_ctx)\b",
)


def main() -> int:
    failed = False
    for path in sorted(ROOT.rglob("*.c")):
        rel = path.relative_to(ROOT)
        rel_s = rel.as_posix()
        if rel_s == "dynchain.c":
            continue
        if rel.parts and rel.parts[0] == "dynchain":
            continue
        if rel_s == "eval.c":
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        for i, line in enumerate(text.splitlines(), 1):
            m = LINE_RE.search(line)
            if not m:
                continue
            fn = m.group(1)
            if fn in ALLOW_FIRST_ARG_CTX:
                continue
            print(f"{path}:{i}: forbidden {fn}(..., first arg context pointer)", file=sys.stderr)
            print(f"  {line.strip()}", file=sys.stderr)
            failed = True

    if failed:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
