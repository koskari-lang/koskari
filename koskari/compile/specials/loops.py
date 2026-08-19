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
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode
from ...types import is_nil
from ...matcher import (BINDING, BODY, CALLEDBY, MATCH, OPTIONAL, USING_UNPACK,
                        VALUE)
from ...syntax import special
from ..helpers import helper_begin, helper_elsebody, using_unpack_helper


_WHILE_PATTERNS = [
    MATCH(CALLEDBY("_while"), VALUE("test"), BODY("body")),
]


@builtin("compile/while")
@special("compile/while", patterns=_WHILE_PATTERNS, unpack="kwargs")
def special_while(compiler, _while, test, body):
    """
    Compile (compile/while TEST BODY... [else: ELSEBODY]). Loop with break and continue support
    """

    called_by = str(_while.datum)

    fragment = compiler.fragment(called_by)
    loop_exit = compiler.gen_label("while.end")

    body = body.datum
    body, elsebody = helper_elsebody(body)
    has_elsebody = not is_nil(elsebody) and elsebody is not None

    fragment.instruction(OpCode.PUSH_LOOP, 0)

    # loop test
    fragment.merge(compiler.full_compile(test.datum))
    fragment.instruction(OpCode.JUMP_FALSE, loop_exit)

    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.CONTINUE, 1)

    fragment.instruction(OpCode.LABEL, loop_exit)

    if has_elsebody:
        fragment.instruction(OpCode.CLEAR_LOOP, 0)
        fragment.merge(compiler.full_compile(elsebody))
        fragment.instruction(OpCode.BREAK, 1)

    fragment.instruction(OpCode.POP_LOOP, 0)

    return fragment


_FOREACH_PATTERNS = (
    MATCH(
        CALLEDBY("_foreach"),
        BINDING("binding", scope="body"),
        BODY("body")),
    MATCH(
        CALLEDBY("_foreach"),
        USING_UNPACK("using", scope="body"),
        BODY("body")),
)


@builtin("compile/for-each")
@special("compile/for-each", patterns=_FOREACH_PATTERNS, unpack="match")
def special_for_each(compiler, caps):
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
    has_elsebody = not is_nil(elsebody) and elsebody is not None

    fragment = compiler.fragment(called_by)
    fragment.position_of(called_by)

    loop_else = compiler.gen_label("foreach.else")

    expr = binding["expr"] if binding else using["expr"]

    # the iterable expression
    fragment.merge(compiler.full_compile(expr.datum))

    # set up the loop and get the first value
    fragment.instruction(OpCode.PUSH_FOREACH, 0)
    fragment.instruction(OpCode.ITER_NEXT, loop_else)

    if using:
        src = using.source
        lhs = using["lhs"].datum
        fragment.merge(using_unpack_helper(compiler, lhs, src))
    else:
        binding_name = str(binding["name"].datum)
        fragment.declare_var(binding_name)
        fragment.instruction(OpCode.VAR_STORE, binding_name)

    # perform the loop body, then continue back up at the ITER_NEXT
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.CONTINUE, 1)

    fragment.instruction(OpCode.LABEL, loop_else)
    if has_elsebody:
        fragment.instruction(OpCode.CLEAR_FOREACH, 0)
        fragment.merge(compiler.full_compile(elsebody))
        fragment.instruction(OpCode.BREAK, 1)

    fragment.instruction(OpCode.POP_FOREACH, 0)

    return fragment


def _special_break_continue(compiler, source, op):
    """
    Shared logic for break and continue. Emit op with count 0 or 1
    """

    called_by, rest = source

    if is_nil(rest):
        frag = compiler.fragment(called_by)
        frag.position_of(source)
        frag.instruction(op, 0)
        return frag

    val, tail = rest
    if not is_nil(tail):
        raise SpecialError(
            "%s takes at most one argument" % called_by,
            source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.merge(compiler.full_compile(val))
    frag.instruction(op, 1)
    return frag


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), OPTIONAL(VALUE("val"))),)


@builtin("compile/break")
@special("compile/break", patterns=_DEFAULT_PATTERNS)
def special_break(compiler, source):
    """
    Compile (compile/break) or (compile/break VAL). Exits innermost loop.
    """

    return _special_break_continue(compiler, source, OpCode.BREAK)


@builtin("compile/continue")
@special("compile/continue", patterns=_DEFAULT_PATTERNS)
def special_continue(compiler, source):
    """
    Compile (compile/continue) or (compile/continue VAL). Restarts loop.
    """

    return _special_break_continue(compiler, source, OpCode.CONTINUE)


# The end.
