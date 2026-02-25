# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
koskari.compile.specials.call

Special form handler for compile/call.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import OpCode, pair, symbol
from ..compiler import ByteCodeFragment, Compiler
from ..helpers import gather_parameters_ng, special


def _partition_args(args_seq):
    """Partition args_seq into chunks: (pos, [items]) or (star, symbol)."""

    chunks = []
    current_pos = []

    for item in args_seq:
        if item[0] == "*":
            if current_pos:
                chunks.append(("pos", current_pos))
                current_pos = []
            chunks.append(("star", item[1]))
        else:
            current_pos.append(item)

    if current_pos:
        chunks.append(("pos", current_pos))

    return chunks


def _partition_kwds(kwds_seq):
    """Partition kwds_seq into chunks: (kw, [(name,expr)...]) or (starstar, sym)."""

    chunks = []
    current_kw = []

    for item in kwds_seq:
        if item[0] == "**":
            if current_kw:
                chunks.append(("kw", current_kw))
                current_kw = []
            chunks.append(("starstar", item[1]))
        else:
            current_kw.append(item)

    if current_kw:
        chunks.append(("kw", current_kw))

    return chunks


@special(symbol("compile/call"))
def special_call(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    fragment = compiler.fragment(called_by)

    fragment.position_of(body)
    obj, rest = body
    fragment.merge(compiler.full_compile(obj))

    args_seq, kwds_seq = gather_parameters_ng(rest, source=rest)
    has_star = any(x[0] == "*" for x in args_seq)
    has_starstar = any(x[0] == "**" for x in kwds_seq)

    if not has_star and not has_starstar:
        if not kwds_seq:
            for _, expr in args_seq:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.position_of(source)
            if tc:
                fragment.instruction(OpCode.KOS_OP_TAIL_CALL, len(args_seq))
            else:
                fragment.instruction(OpCode.KOS_OP_CALL, len(args_seq))
        else:
            pos_exprs = [expr for (idx, expr) in args_seq]
            kw_names = []
            kw_values = []
            for item in kwds_seq:
                kw_names.append(item[0])
                kw_values.append(item[1])
            opa = len(pos_exprs)
            for expr in pos_exprs:
                fragment.merge(compiler.full_compile(expr, False))
            for expr in kw_values:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.KOS_OP_CONST, tuple(kw_names))
            fragment.position_of(source)
            if tc:
                fragment.instruction(OpCode.KOS_OP_TAIL_CALL_NAMES, opa)
            else:
                fragment.instruction(OpCode.KOS_OP_CALL_NAMES, opa)
    else:
        opa = 0
        if args_seq:
            opa = opa | 1
        if kwds_seq:
            opa = opa | 2

        if args_seq:
            arg_chunks = _partition_args(args_seq)
            for kind, data in arg_chunks:
                if kind == "pos":
                    for _, expr in data:
                        fragment.merge(compiler.full_compile(expr, False))
                    fragment.instruction(OpCode.KOS_OP_BUILD_ARGS, len(data))
                else:
                    fragment.merge(compiler.full_compile(data, False))
            if len(arg_chunks) > 1:
                fragment.instruction(OpCode.KOS_OP_COMBINE_ARGS, len(arg_chunks))

        if kwds_seq:
            kwd_chunks = _partition_kwds(kwds_seq)
            for kind, data in kwd_chunks:
                if kind == "kw":
                    names = tuple(n for n, _ in data)
                    for _, expr in data:
                        fragment.merge(compiler.full_compile(expr, False))
                    fragment.instruction(OpCode.KOS_OP_BUILD_KWDS_NAMES, names)
                else:
                    fragment.merge(compiler.full_compile(data, False))
            if len(kwd_chunks) > 1:
                fragment.instruction(OpCode.KOS_OP_COMBINE_KWDS, len(kwd_chunks))

        fragment.position_of(source)
        if tc:
            fragment.instruction(OpCode.KOS_OP_TAIL_CALL_STAR, opa)
        else:
            fragment.instruction(OpCode.KOS_OP_CALL_STAR, opa)

    return fragment


# The end.
