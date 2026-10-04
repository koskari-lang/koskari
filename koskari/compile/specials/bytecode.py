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
koskari.compile.specials.bytecode

Special form handler for compile/bytecode.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...lib import OpCode, keyword, symbol
from ...matcher import (
    CALLEDBY, FORM, MATCH, OPTIONAL, VALUE, repeat_capture)
from ...syntax import special
from ..compiler import ByteCodeFragment, Compiler


_BYTECODE_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        repeat_capture(
            "segments",
            FORM(MATCH(VALUE("op"), OPTIONAL(VALUE("opa")))))),
]


@builtin("compile/bytecode")
@special("compile/bytecode", patterns=_BYTECODE_PATTERNS, unpack="kwargs")
def special_bytecode(
        compiler: Compiler,
        called_by,
        segments) -> ByteCodeFragment:

    fragment = compiler.fragment(called_by.datum)

    for segm in segments:
        opcap = segm["op"]
        op = opcap.datum
        opacap = segm.get("opa")
        opa = 0 if opacap is None else opacap.datum

        if isinstance(op, int):
            opc = OpCode(op)

        elif isinstance(op, (str, symbol, keyword)):
            op = str(op)
            opc = getattr(OpCode, op, None)

        else:
            opc = None

        if opc is None:
            raise TypeError(f"invalid Koskari OpCode: {op!r}")

        # the op capture's position is the segment cell's own position
        fragment.position(opcap.position)
        fragment.instruction(opc, opa)

    return fragment


# The end.
