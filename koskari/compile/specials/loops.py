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
koskari.compile.specials.loops

Special form handlers for compile/while, compile/break, and compile/continue.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ...types import is_pair
from ..exceptions import CompilerException
from ..helpers import helper_begin, special


@special(symbol("compile/while"))
def special_while(compiler, source, tc=False):
    """
    Compile (compile/while TEST BODY...). Loop with break and continue support
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("while requires test and body", source)

    test, body = rest
    fragment = compiler.fragment(called_by)
    loop_head = compiler.gen_label("while.head")
    loop_exit = compiler.gen_label("while.end")

    with fragment.child_loop(called_by) as child:
        child.instruction(OpCode.KOS_OP_LABEL, loop_head)
        child.merge(compiler.full_compile(test, False))
        child.instruction(OpCode.KOS_OP_JUMP_FALSE, loop_exit)
        helper_begin(child, body, False)
        child.instruction(OpCode.KOS_OP_JUMP, loop_head)
        child.instruction(OpCode.KOS_OP_LABEL, loop_exit)
        child.instruction(OpCode.KOS_OP_CLEAR_LOOP, 0)

    return fragment


def _special_break_continue(compiler, source, op):
    """
    Shared logic for break and continue. Emit op with count 0 or 1
    """

    called_by, rest = source

    if rest is nil:
        frag = compiler.fragment(called_by)
        frag.position_of(source)
        frag.instruction(op, 0)
        return frag

    val, tail = rest
    if tail is not nil:
        raise CompilerException(
            "%s takes at most one argument" % called_by,
            source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.merge(compiler.full_compile(val, False))
    frag.instruction(op, 1)
    return frag


@special(symbol("compile/break"))
def special_break(compiler, source, tc=False):
    """
    Compile (compile/break) or (compile/break VAL). Exits innermost loop.
    """

    return _special_break_continue(compiler, source, OpCode.KOS_OP_BREAK)


@special(symbol("compile/continue"))
def special_continue(compiler, source, tc=False):
    """
    Compile (compile/continue) or (compile/continue VAL). Restarts loop.
    """

    return _special_break_continue(compiler, source, OpCode.KOS_OP_CONTINUE)


# The end.
