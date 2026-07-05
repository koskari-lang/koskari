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
koskari.compile.specials.collections

Special form handlers for compile/#list and compile/#list-append.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, REST, SYMBOL, VALUE
from ...syntax import special
from ...types import is_nil, is_pair
from ..helpers import is_symbolish, splat_call_form, unpack


_DEFAULT_LIST_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/#tuple")
@special("compile/#tuple", patterns=_DEFAULT_LIST_PATTERNS)
def special_build_tuple(compiler, source, tc=False):
    """
    Build a Python tuple from evaluated elements
    """

    called_by, body = source

    call_form = splat_call_form(called_by, body)
    if call_form is not None:
        return call_form

    frag = compiler.fragment(called_by)
    count = 0

    if is_nil(body):
        frag.instruction(OpCode.KOS_OP_BUILD_ARGS, 0)
        return frag

    for expr in unpack(body):
        if not is_pair(expr):
            frag.position_of(body)

        frag.merge(compiler.full_compile(expr, False))
        count += 1

    frag.instruction(OpCode.KOS_OP_BUILD_ARGS, count)
    return frag


@builtin("compile/#list")
@special("compile/#list", patterns=_DEFAULT_LIST_PATTERNS)
def special_build_list(compiler, source, tc=False):
    """
    Build a Python list from evaluated elements
    """

    called_by, body = source

    call_form = splat_call_form(called_by, body)
    if call_form is not None:
        return call_form

    frag = compiler.fragment(called_by)
    count = 0

    if is_nil(body):
        frag.instruction(OpCode.KOS_OP_BUILD_LIST, 0)
        return frag

    for expr in unpack(body):
        if not is_pair(expr):
            frag.position_of(body)

        frag.merge(compiler.full_compile(expr, False))
        count += 1

    frag.instruction(OpCode.KOS_OP_BUILD_LIST, count)
    return frag


_LIST_APPEND_PATTERNS = [
    MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr")),
    MATCH(CALLEDBY(), VALUE("list_expr"), VALUE("expr")),
]


@builtin("compile/#list-append")
@special("compile/#list-append", patterns=_LIST_APPEND_PATTERNS, unpack="match")
def special_list_append(compiler, caps, tc=False):
    """
    Append the value of EXPR to a list binding or list expression
    """

    called_by = caps["called-by"].datum
    expr = caps["expr"].datum
    frag = compiler.fragment(called_by)

    if "sym" in caps:
        sym = caps["sym"].datum
        if not is_symbolish(sym):
            raise SpecialError("#list-append name must be a symbol", caps.source)

        name = str(sym)
        pos = caps["sym"].position
        frag.request_for_write(name, position=pos)
        if not is_pair(expr):
            frag.position(pos)

        frag.merge(compiler.full_compile(expr, False))
        frag.instruction(OpCode.KOS_OP_VAR_LIST_APPEND, name)
        frag.instruction(OpCode.KOS_OP_CONST, None)
        return frag

    list_expr = caps["list_expr"].datum
    frag.merge(compiler.full_compile(list_expr, False))
    if not is_pair(expr):
        frag.position_of(called_by)

    frag.merge(compiler.full_compile(expr, False))
    frag.instruction(OpCode.KOS_OP_BUILD_LIST_APPEND, 0)
    return frag


# The end.
