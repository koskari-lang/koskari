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
from ...lib import OpCode
from ...matcher import (
    BINDING, BODY, CALLEDBY, MATCH, OPTIONAL, USING_UNPACK, VALUE)
from ...syntax import special
from ...types import is_nil
from ..helpers import helper_begin, helper_elsebody, using_unpack_helper


_WHILE_PATTERNS = [
    MATCH(CALLEDBY("_while"), VALUE("test"), BODY("body")),
]


@builtin("compile/while")
@special("compile/while", patterns=_WHILE_PATTERNS, unpack="kwargs")
def special_while(compiler, _while, test, body):
    """
    Compile (compile/while TEST BODY... [else: ELSEBODY...]). Loop with break and continue support
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

    helper_begin(fragment, body)
    fragment.instruction(OpCode.CONTINUE, 1)

    fragment.instruction(OpCode.LABEL, loop_exit)

    if has_elsebody:
        fragment.instruction(OpCode.CLEAR_LOOP, 0)
        helper_begin(fragment, elsebody)
        fragment.instruction(OpCode.BREAK, 1)

    fragment.instruction(OpCode.POP_LOOP, 0)

    return fragment


_FOREACH_PATTERNS = (
    MATCH(
        CALLEDBY("_foreach"),
        BINDING("binding"),
        BODY("body")),
    MATCH(
        CALLEDBY("_foreach"),
        USING_UNPACK("using"),
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
        binding_name = binding["name"].datum
        fragment.declare_var(binding_name)
        fragment.instruction(OpCode.VAR_STORE, binding_name)

    # perform the loop body, then continue back up at the ITER_NEXT
    helper_begin(fragment, body)
    fragment.instruction(OpCode.CONTINUE, 1)

    fragment.instruction(OpCode.LABEL, loop_else)
    if has_elsebody:
        fragment.instruction(OpCode.CLEAR_FOREACH, 0)
        helper_begin(fragment, elsebody)
        fragment.instruction(OpCode.BREAK, 1)

    fragment.instruction(OpCode.POP_FOREACH, 0)

    return fragment


_LOOP_JUMP_PATTERNS = [
    MATCH(CALLEDBY("called_by"), OPTIONAL(VALUE("val"))),
]


def emit_loop_jump(compiler, called_by, val, op):
    """
    A fragment for break and continue: op with a count of 0, or the
    optional value compiled and a count of 1

    :return: the fragment
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)

    if val is None:
        frag.instruction(op, 0)
        return frag

    frag.merge(compiler.full_compile(val.datum))
    frag.instruction(op, 1)
    return frag


@builtin("compile/break")
@special("compile/break", patterns=_LOOP_JUMP_PATTERNS, unpack="match")
def special_break(compiler, caps):
    """
    Compile (compile/break) or (compile/break VAL). Exits innermost loop.
    """

    return emit_loop_jump(
        compiler, caps["called_by"], caps.get("val"), OpCode.BREAK)


@builtin("compile/continue")
@special("compile/continue", patterns=_LOOP_JUMP_PATTERNS, unpack="match")
def special_continue(compiler, caps):
    """
    Compile (compile/continue) or (compile/continue VAL). Restarts loop.
    """

    return emit_loop_jump(
        compiler, caps["called_by"], caps.get("val"), OpCode.CONTINUE)


# The end.
