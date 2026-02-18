# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
opimpl - regenerate handlers.incl from opcodes.incl

Reads koskari/lib/opcodes.incl and emits koskari/lib/handlers.incl with one
#include "opcode/OPNAME.c" per OP definition, preserving order.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
OPCODES_INCL = REPO_ROOT / "koskari/lib/opcodes.incl"
HANDLERS_INCL = REPO_ROOT / "koskari/lib/handlers.incl"


def get_opcode_order(path):
    """
    Extract OP(name, style) names from opcodes.incl in order.

    :param path: path to opcodes.incl
    :return: list of opcode names (case-preserving)
    """
    content = path.read_text()
    return re.findall(r"^OP\(([A-Za-z0-9_]+)\s*[,)]", content, re.MULTILINE)


def main():
    ops = get_opcode_order(OPCODES_INCL)

    with HANDLERS_INCL.open("w") as f:
        out = lambda l: print(l, file=f)
        out("/* Generated from opcodes.incl - one #include per opcode in order */")
        out("")
        for op in ops:
            out(f'#include "opcode/{op}.c"')
        out("")
        out("/* The end. */")

    print(f"Wrote {HANDLERS_INCL} ({len(ops)} opcodes)")


if __name__ == "__main__":
    main()


# The end.
