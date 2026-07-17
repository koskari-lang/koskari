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

Special form handlers for compile/#tuple, compile/#list,
compile/#list-append, and compile/#dict.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode
from ...matcher import (CALLEDBY, LITERAL_KW, LITERAL_SYM, MATCH, ONEOF, REST,
                        SYMBOL, VALUE, repeat_capture)
from ...syntax import special
from ...types import is_nil, is_pair
from ..helpers import (call_form_reroute, gather_parameters, is_symbolish,
                       partition_collection_runs)


_DEFAULT_LIST_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/#tuple")
@special("compile/#tuple", patterns=_DEFAULT_LIST_PATTERNS)
def special_build_tuple(compiler, source, tc=False):
    """
    Build a Python tuple from evaluated elements.

    Contiguous constant elements fold into a single pooled tuple; the tuple
    is assembled with COMBINE_ARGS only when constant runs and dynamic
    expressions are interleaved. An all-constant literal reduces to one CONST.
    """

    called_by, body = source

    frag = compiler.fragment(called_by)

    if is_nil(body):
        frag.instruction(OpCode.KOS_OP_BUILD_ARGS, 0)
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
            frag.instruction(OpCode.KOS_OP_CONST, data)
        elif kind == "splat":
            frag.merge(data)
        else:
            for elem in data:
                frag.merge(elem)
            frag.instruction(OpCode.KOS_OP_BUILD_ARGS, len(data))

    # A lone splat leaves the raw iterable on the stack; COMBINE_ARGS coerces
    # it to a fresh tuple. Multiple chunks always combine. A single const or
    # expr chunk is already a tuple.
    needs_combine = (len(chunks) > 1
                     or (len(chunks) == 1 and chunks[0][0] == "splat"))
    if needs_combine:
        frag.instruction(OpCode.KOS_OP_COMBINE_ARGS, len(chunks))

    return frag


@builtin("compile/#list")
@special("compile/#list", patterns=_DEFAULT_LIST_PATTERNS)
def special_build_list(compiler, source, tc=False):
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
        frag.instruction(OpCode.KOS_OP_BUILD_LIST, 0)
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
        frag.instruction(OpCode.KOS_OP_BUILD_LIST_CONST, first_data)
    elif first_kind == "splat":
        frag.instruction(OpCode.KOS_OP_BUILD_LIST, 0)
        frag.merge(first_data)
        frag.instruction(OpCode.KOS_OP_BUILD_LIST_EXTEND, 0)
    else:
        for elem in first_data:
            frag.merge(elem)
        frag.instruction(OpCode.KOS_OP_BUILD_LIST, len(first_data))

    # Extend / append the remaining chunks in place.
    for kind, data in chunks[1:]:
        if kind == "const":
            if len(data) == 1:
                frag.instruction(OpCode.KOS_OP_BUILD_LIST_APPEND_CONST, data[0])
            else:
                frag.instruction(OpCode.KOS_OP_BUILD_LIST_EXTEND_CONST, data)
        elif kind == "splat":
            frag.merge(data)
            frag.instruction(OpCode.KOS_OP_BUILD_LIST_EXTEND, 0)
        else:
            for elem in data:
                frag.merge(elem)
            frag.instruction(OpCode.KOS_OP_BUILD_LIST_APPEND, len(data))

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
    frag.instruction(OpCode.KOS_OP_BUILD_LIST_APPEND, 1)
    return frag


_DICT_PATTERNS = (
    MATCH(CALLEDBY(), repeat_capture("pairs", ONEOF(
        MATCH(LITERAL_KW("**"), VALUE("splat")),
        MATCH(VALUE("key"), LITERAL_SYM(":"), VALUE("val")),
    ))),
)


@builtin("compile/#dict")
@special("compile/#dict", patterns=_DICT_PATTERNS, unpack="match")
def special_build_dict(compiler, caps, tc=False):
    """
    Build a Python dict from a sequence of key/value pairs.

    Each pair takes one of two shapes: ``KEY : VALUE`` (an explicit ``:``
    symbol between two forms, where KEY is evaluated as an expression) or
    ``**: EXPR`` (merge the mapping EXPR). The surface ``keyword: VALUE``
    spelling is normalised into the ``KEY : VALUE`` form by the ``#{ }``
    reader macro, so it never reaches this special directly. Contiguous
    key/value pairs assemble with BUILD_KWDS; ``**:`` splats and multiple
    runs merge left-to-right with COMBINE_KWDS.
    """

    called_by = caps["called-by"].datum
    frag = compiler.fragment(called_by)

    position = caps["called-by"].position
    if position:
        frag.position(position)

    pairs = caps.get("pairs")
    if not pairs:
        frag.instruction(OpCode.KOS_OP_BUILD_KWDS, 0)
        return frag

    # Partition into contiguous segments in source order. A "kv" segment
    # is a run of key/value cells assembled with a single BUILD_KWDS; a
    # "splat" segment is one mapping expression to merge.
    segments = []
    run = []
    for cell in pairs:
        if "splat" in cell:
            if run:
                segments.append(("kv", run))
                run = []
            segments.append(("splat", cell["splat"].datum))
        else:
            run.append(cell)
    if run:
        segments.append(("kv", run))

    for kind, data in segments:
        if kind == "splat":
            frag.merge(compiler.full_compile(data, False))
            continue

        for cell in data:
            frag.merge(compiler.full_compile(cell["key"].datum, False))
            frag.merge(compiler.full_compile(cell["val"].datum, False))

        frag.instruction(OpCode.KOS_OP_BUILD_KWDS, len(data))

    # A lone splat leaves the source mapping on the stack; COMBINE_KWDS
    # copies it into a fresh dict so the literal never aliases it.
    has_splat = any(kind == "splat" for kind, _ in segments)
    if len(segments) > 1 or (len(segments) == 1 and has_splat):
        frag.instruction(OpCode.KOS_OP_COMBINE_KWDS, len(segments))

    return frag


# The end.
