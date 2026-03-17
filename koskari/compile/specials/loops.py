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
from ...exceptions import SpecialError
from ..helpers import BINDING, helper_begin, helper_elsebody, special, special_forms
from ...matcher import MATCH, REST, match


@special(symbol("compile/while"))
def special_while(compiler, source, tc=False):
    """
    Compile (compile/while TEST BODY...). Loop with break and continue support
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("while requires test and body", source)

    test, body = rest
    fragment = compiler.fragment(called_by)
    loop_head = compiler.gen_label("while.head")
    loop_exit = compiler.gen_label("while.end")

    with fragment.child_loop(called_by) as child:
        child.instruction(OpCode.KOS_OP_LABEL, loop_head)
        child.merge(compiler.full_compile(test, False))
        child.instruction(OpCode.KOS_OP_JUMP_FALSE, loop_exit)
        helper_begin(child, body, False)

        child.instruction(OpCode.KOS_OP_CONTINUE, 1)
        child.instruction(OpCode.KOS_OP_LABEL, loop_exit)
        child.instruction(OpCode.KOS_OP_CLEAR_LOOP, 0)

    return fragment


_FOREACH_PATTERNS = [
    MATCH(BINDING("binding", scope="body"), REST("body")),
]


@special_forms(_FOREACH_PATTERNS)
@special(symbol("compile/for-each"))
def special_for_each(compiler, source, tc=False):
    """
    Compile (compile/for-each [BINDING EXPR] BODY... [else: ELSEBODY]).
    Iterates EXPR, binding each element to BINDING and running BODY.
    On natural exhaustion, runs ELSEBODY if provided.
    Break exits the loop early without running the else clause.
    The loop expression value is None unless break or else provides a value.
    """

    called_by, rest = source
    matched = match(rest, _FOREACH_PATTERNS)
    if matched is None:
        raise SpecialError("for-each requires a binding and body", source)

    binding = matched["binding"]
    if binding.name is None:
        raise SpecialError("for-each requires a named binding", source)

    body = matched["body"].source
    body, elsebody = helper_elsebody(body)
    has_elsebody = elsebody is not nil and elsebody is not None

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)

    loop_else = compiler.gen_label("foreach.else")

    binding_name = str(binding.name)
    binding_expr = binding.value

    fragment.instruction(OpCode.KOS_OP_CONST, None)
    fragment.merge(compiler.full_compile(binding_expr, False))
    fragment.instruction(OpCode.KOS_OP_PUSH_FOREACH, 0)
    fragment.instruction(OpCode.KOS_OP_ITER_NEXT, loop_else)

    fragment.declare_var(binding_name)
    fragment.instruction(OpCode.KOS_OP_VAR_STORE, binding_name)

    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_CONTINUE, 1)

    fragment.instruction(OpCode.KOS_OP_LABEL, loop_else)
    fragment.instruction(OpCode.KOS_OP_CLEAR_LOOP, 0)

    if has_elsebody:
        fragment.merge(compiler.full_compile(elsebody, False))
        fragment.instruction(OpCode.KOS_OP_BREAK, 1)

    fragment.instruction(OpCode.KOS_OP_POP_LOOP, 0)

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
        raise SpecialError(
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
