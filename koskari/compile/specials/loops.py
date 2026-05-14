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

Special form handlers for compile/while, compile/for-each, compile/break, and
compile/continue.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
"""


from ...exceptions import SpecialError
from ...lib import OpCode, nil
from ...matcher import (BINDING, BODY, MATCH, REST, SYMBOL,
                           USING_UNPACK, VALUE)
from ...stdlib import builtin
from ...syntax import special
from ..helpers import helper_begin, helper_elsebody, using_unpack_helper


_DEFAULT_PATTERNS = (MATCH(SYMBOL("called-by"), REST("rest")),)

_WHILE_PATTERNS = [
    MATCH(SYMBOL("_while"), VALUE("test"), REST("body")),
]


@builtin("compile/while")
@special("compile/while", patterns=_WHILE_PATTERNS, unpack="kwargs")
def special_while(compiler, tc, _while, test, body):
    """
    Compile (compile/while TEST BODY...). Loop with break and continue support
    """

    called_by = str(_while.datum)

    fragment = compiler.fragment(called_by)
    loop_head = compiler.gen_label("while.head")
    loop_exit = compiler.gen_label("while.end")

    with fragment.child_loop(called_by) as child:
        child.instruction(OpCode.KOS_OP_LABEL, loop_head)
        child.merge(compiler.full_compile(test.datum, False))
        child.instruction(OpCode.KOS_OP_JUMP_FALSE, loop_exit)
        helper_begin(child, body.datum, False)

        child.instruction(OpCode.KOS_OP_CONTINUE, 1)
        child.instruction(OpCode.KOS_OP_LABEL, loop_exit)
        child.instruction(OpCode.KOS_OP_CLEAR_LOOP, 0)

    return fragment


_FOREACH_PATTERNS = (
    MATCH(
        SYMBOL("_foreach"),
        BINDING("binding", scope="body"),
        BODY("body")),
    MATCH(
        SYMBOL("_foreach"),
        USING_UNPACK("using", scope="body"),
        BODY("body")),
)


@builtin("compile/for-each")
@special("compile/for-each", patterns=_FOREACH_PATTERNS, unpack="match")
def special_for_each(compiler, caps, tc=False):
    """
    Compile (compile/for-each [BINDING EXPR] BODY... [else: ELSEBODY]) or
    (compile/for-each [[PATTERN...] EXPR] BODY... [else: ELSEBODY]).
    Iterates EXPR, binding each element to BINDING or unpacking via PATTERN.
    On natural exhaustion, runs ELSEBODY if provided.
    Break exits the loop early without running the else clause.
    The loop expression value is None unless break or else provides a value.
    """

    called_by = caps["_foreach"].datum

    binding = caps.get("binding")
    using = caps.get("using")

    body = caps["body"].datum
    body, elsebody = helper_elsebody(body)
    has_elsebody = elsebody is not nil and elsebody is not None

    fragment = compiler.fragment(called_by)
    fragment.position_of(called_by)

    loop_else = compiler.gen_label("foreach.else")

    expr = binding["expr"] if binding else using["expr"]

    fragment.instruction(OpCode.KOS_OP_CONST, None)
    fragment.merge(compiler.full_compile(expr.datum, False))
    fragment.instruction(OpCode.KOS_OP_PUSH_FOREACH, 0)
    fragment.instruction(OpCode.KOS_OP_ITER_NEXT, loop_else)

    if using:
        src = using.source
        lhs = using["lhs"].datum
        fragment.merge(using_unpack_helper(compiler, lhs, src))
    else:
        binding_name = str(binding["name"].datum)
        fragment.declare_var(binding_name)
        fragment.instruction(OpCode.KOS_OP_VAR_STORE, binding_name)

    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_CONTINUE, 1)

    fragment.instruction(OpCode.KOS_OP_LABEL, loop_else)
    fragment.instruction(OpCode.KOS_OP_CLEAR_FOREACH, 0)

    if has_elsebody:
        fragment.merge(compiler.full_compile(elsebody, False))
        fragment.instruction(OpCode.KOS_OP_BREAK, 1)

    fragment.instruction(OpCode.KOS_OP_POP_FOREACH, 0)

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


@builtin("compile/break")
@special("compile/break", patterns=_DEFAULT_PATTERNS)
def special_break(compiler, source, tc=False):
    """
    Compile (compile/break) or (compile/break VAL). Exits innermost loop.
    """

    return _special_break_continue(compiler, source, OpCode.KOS_OP_BREAK)


@builtin("compile/continue")
@special("compile/continue", patterns=_DEFAULT_PATTERNS)
def special_continue(compiler, source, tc=False):
    """
    Compile (compile/continue) or (compile/continue VAL). Restarts loop.
    """

    return _special_break_continue(compiler, source, OpCode.KOS_OP_CONTINUE)


# The end.
