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

Special form handlers for compile/#tuple, compile/#list, and
compile/#list-append.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, REST, SYMBOL, VALUE
from ...syntax import special
from ...types import is_nil, is_pair, is_symbol
from ..helpers import (
    call_form_reroute, gather_parameters, partition_collection_runs)


_DEFAULT_LIST_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/#tuple")
@special("compile/#tuple", patterns=_DEFAULT_LIST_PATTERNS)
def special_build_tuple(compiler, source):
    """
    Build a Python tuple from evaluated elements.

    Contiguous constant elements fold into a single pooled tuple; the tuple
    is assembled with COMBINE_ARGS only when constant runs and dynamic
    expressions are interleaved. An all-constant literal reduces to one CONST.
    """

    called_by, body = source

    frag = compiler.fragment(called_by)

    if is_nil(body):
        frag.instruction(OpCode.BUILD_ARGS, 0)
        return frag

    args_seq, kwds_seq = gather_parameters(body, source=body)
    if kwds_seq:
        # keyword / **: arguments cannot be assembled inline; defer to the
        # runtime builtin via the call special
        return call_form_reroute(called_by, body)

    frag.position_of(body)
    chunks = partition_collection_runs(compiler, args_seq)

    for kind, data in chunks:
        if kind == "const":
            frag.instruction(OpCode.CONST, data)
        elif kind == "splat":
            frag.merge(data)
        else:
            for elem in data:
                frag.merge(elem)
            frag.instruction(OpCode.BUILD_ARGS, len(data))

    # A lone splat leaves the raw iterable on the stack; COMBINE_ARGS coerces
    # it to a fresh tuple. Multiple chunks always combine. A single const or
    # expr chunk is already a tuple.
    needs_combine = (len(chunks) > 1
                     or (len(chunks) == 1 and chunks[0][0] == "splat"))
    if needs_combine:
        frag.instruction(OpCode.COMBINE_ARGS, len(chunks))

    return frag


@builtin("compile/#list")
@special("compile/#list", patterns=_DEFAULT_LIST_PATTERNS)
def special_build_list(compiler, source):
    """
    Build a Python list from evaluated elements.

    Elements are partitioned into constant runs and dynamic expressions. A
    fresh list is seeded from the first chunk, then extended in place: constant
    runs come straight from the pool (BUILD_LIST_EXTEND_CONST /
    BUILD_LIST_APPEND_CONST) and dynamic runs are appended from the stack
    (BUILD_LIST_APPEND). An all-constant literal reduces to one
    BUILD_LIST_CONST.
    """

    called_by, body = source

    frag = compiler.fragment(called_by)

    if is_nil(body):
        frag.instruction(OpCode.BUILD_LIST, 0)
        return frag

    args_seq, kwds_seq = gather_parameters(body, source=body)
    if kwds_seq:
        # keyword / **: arguments cannot be assembled inline; defer to the
        # runtime builtin via the call special
        return call_form_reroute(called_by, body)

    frag.position_of(body)
    chunks = partition_collection_runs(compiler, args_seq)

    # Seed a fresh list from the first chunk.
    first_kind, first_data = chunks[0]
    if first_kind == "const":
        frag.instruction(OpCode.BUILD_LIST_CONST, first_data)
    elif first_kind == "splat":
        frag.instruction(OpCode.BUILD_LIST, 0)
        frag.merge(first_data)
        frag.instruction(OpCode.BUILD_LIST_EXTEND, 0)
    else:
        for elem in first_data:
            frag.merge(elem)
        frag.instruction(OpCode.BUILD_LIST, len(first_data))

    # Extend / append the remaining chunks in place.
    for kind, data in chunks[1:]:
        if kind == "const":
            if len(data) == 1:
                frag.instruction(OpCode.BUILD_LIST_APPEND_CONST, data[0])
            else:
                frag.instruction(OpCode.BUILD_LIST_EXTEND_CONST, data)
        elif kind == "splat":
            frag.merge(data)
            frag.instruction(OpCode.BUILD_LIST_EXTEND, 0)
        else:
            for elem in data:
                frag.merge(elem)
            frag.instruction(OpCode.BUILD_LIST_APPEND, len(data))

    return frag


_LIST_APPEND_PATTERNS = [
    MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr")),
    MATCH(CALLEDBY(), VALUE("list_expr"), VALUE("expr")),
]


@builtin("compile/#list-append")
@special("compile/#list-append", patterns=_LIST_APPEND_PATTERNS, unpack="match")
def special_list_append(compiler, caps):
    """
    Append the value of EXPR to a list binding or list expression
    """

    called_by = caps["called-by"].datum
    expr = caps["expr"].datum
    frag = compiler.fragment(called_by)

    if "sym" in caps:
        sym = caps["sym"].datum
        if not is_symbol(sym):
            raise SpecialError("#list-append name must be a symbol", caps.source)

        pos = caps["sym"].position
        frag.request_for_write(sym, position=pos)
        if not is_pair(expr):
            frag.position(pos)

        frag.merge(compiler.full_compile(expr))
        frag.instruction(OpCode.VAR_LIST_APPEND, sym)
        frag.instruction(OpCode.CONST, None)
        return frag

    list_expr = caps["list_expr"].datum
    frag.merge(compiler.full_compile(list_expr))
    if not is_pair(expr):
        frag.position_of(called_by)

    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.BUILD_LIST_APPEND, 1)
    return frag


# The end.
