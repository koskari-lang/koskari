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

Special form handler for compile/call and compile/call-attr

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...exceptions import SpecialError
from ...lib import OpCode, cons, nil, pair, symbol
from ...matcher import CALLEDBY, LIST, LITERAL_SYM, MATCH, BODY, SYMBOL, VALUE
from ...bootstrap import builtin
from ...syntax import special
from ...types import fill_position, is_symbol
from ..compiler import ByteCodeFragment, Compiler
from ..helpers import gather_parameters, match


def _partition_args(args_seq):
    """
    Partition args_seq into chunks: (pos, [items]) or (star, symbol)
    """

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
    """
    Partition kwds_seq into chunks: (kw, [(name,expr)...]) or (starstar, sym)
    """

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


_sym_call_attr = symbol("call-attr")


_CALL_PATTERNS = [
    # call (attr obj attr)
    MATCH(
        CALLEDBY(),
        LIST(LITERAL_SYM("attr"), VALUE("obj"), SYMBOL("attr"))),

    # call (attr obj attr) args...
    MATCH(
        CALLEDBY(),
        LIST(LITERAL_SYM("attr"), VALUE("obj"), SYMBOL("attr")),
        BODY("args")),

    MATCH(CALLEDBY(), VALUE("callable")),
    MATCH(CALLEDBY(), VALUE("callable"), BODY("args")),
]


@builtin("compile/call")
@special("compile/call", patterns=_CALL_PATTERNS, unpack="match")
def special_call(
        compiler: Compiler,
        caps: dict,
        tc: bool = False) -> ByteCodeFragment:

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    rest = caps["args"].datum if "args" in caps else nil

    # rewriting ((attr obj a) args...) to
    # (compile/call-attr obj a args...)
    if "attr" in caps:
        obj = caps["obj"]
        attr = caps["attr"]
        rewritten = cons(
            _sym_call_attr,
            obj.datum, attr.datum, rest)
        fill_position(rewritten, pos)
        return rewritten

    callable = caps["callable"].datum

    use_var_path = is_symbol(callable)
    name = str(callable) if is_symbol(callable) else None

    # rewriting (.foo obj args...) to
    # (compile/call-attr obj foo args...)
    if name and rest and name.startswith("."):
        obj, rest = rest
        rewritten = cons(
            _sym_call_attr,
            obj, symbol(name[1:]), rest)
        fill_position(rewritten, pos)
        return rewritten

    fragment = compiler.fragment(called_by)
    if use_var_path:
        # the var path names the callable directly rather than routing it
        # through compile_symbol, so the stray-colon check has to run here.
        # The other path calls full_compile, which checks for us.
        compiler.check_stray_colon(callable, pos)
        fragment.request_var(name)

    fragment.position(pos)
    if not use_var_path:
        fragment.merge(compiler.full_compile(callable))

    args_seq, kwds_seq = gather_parameters(rest, source=rest)
    has_star = any(x[0] == "*" for x in args_seq)
    has_starstar = any(x[0] == "**" for x in kwds_seq)

    if not has_star and not has_starstar:
        if not kwds_seq:
            for _, expr in args_seq:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.position(pos)
            if use_var_path:
                fragment.instruction(
                    OpCode.CALL_VAR, (name, len(args_seq), 0, 0))
            else:
                fragment.instruction(OpCode.CALL, len(args_seq))
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
            if not use_var_path:
                fragment.instruction(OpCode.CONST, tuple(kw_names))
            fragment.position(pos)
            if use_var_path:
                fragment.instruction(
                    OpCode.CALL_VAR_NAMES,
                    (name, tuple(kw_names), opa, 0))
            else:
                fragment.instruction(OpCode.CALL_NAMES, opa)
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
                    fragment.instruction(OpCode.BUILD_ARGS, len(data))
                else:
                    fragment.merge(compiler.full_compile(data, False))
            if len(arg_chunks) > 1:
                fragment.instruction(OpCode.COMBINE_ARGS, len(arg_chunks))

        if kwds_seq:
            kwd_chunks = _partition_kwds(kwds_seq)
            for kind, data in kwd_chunks:
                if kind == "kw":
                    names = tuple(n for n, _ in data)
                    for _, expr in data:
                        fragment.merge(compiler.full_compile(expr, False))
                    fragment.instruction(OpCode.BUILD_KWDS_NAMES, names)
                else:
                    fragment.merge(compiler.full_compile(data, False))
            if len(kwd_chunks) > 1:
                fragment.instruction(OpCode.COMBINE_KWDS, len(kwd_chunks))

        fragment.position(pos)
        if use_var_path:
            fragment.instruction(
                OpCode.CALL_VAR_STAR, (name, opa, 0, 0))
        else:
            fragment.instruction(OpCode.CALL_STAR, opa)

    return fragment


_CALL_ATTR_PATTERNS = [
    MATCH(VALUE("obj"), SYMBOL("attr")),
    MATCH(VALUE("obj"), SYMBOL("attr"), BODY("args")),
]

_CALL_ATTR_PATTERNS_FULL = tuple(
    MATCH(CALLEDBY(), *m._children)
    for m in _CALL_ATTR_PATTERNS)


@builtin("compile/call-attr")
@special("compile/call-attr", patterns=_CALL_ATTR_PATTERNS_FULL)
def special_call_attr(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    fragment = compiler.fragment(called_by)

    matched = match(body, _CALL_ATTR_PATTERNS)
    if matched is None:
        raise SpecialError("compile/call-attr requires obj, attr, and optionally args", source)

    obj = matched["obj"].datum
    attr = matched["attr"].datum
    args_src = matched["args"].datum if "args" in matched else nil

    # The attr call always takes the var path when obj is a symbol; the
    # processor's promotion pass relabels tail-position attr calls onto
    # their TAIL_CALL_*ATTR* twins after emission.
    use_var_path = is_symbol(obj)

    fragment.position_of(body)
    if use_var_path:
        fragment.request_var(str(obj))
    else:
        fragment.merge(compiler.full_compile(obj))

    args_seq, kwds_seq = gather_parameters(args_src, source=args_src)
    has_star = any(x[0] == "*" for x in args_seq)
    has_starstar = any(x[0] == "**" for x in kwds_seq)

    if not has_star and not has_starstar:
        if not kwds_seq:
            for _, expr in args_seq:
                fragment.merge(compiler.full_compile(expr, False))
            fragment.position_of(source)
            if use_var_path:
                fragment.instruction(
                    OpCode.CALL_VAR_ATTR, (str(obj), str(attr), len(args_seq), 0))
            else:
                fragment.instruction(
                    OpCode.CALL_ATTR, (str(attr), len(args_seq), 0, 0))
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
            fragment.position_of(source)
            if use_var_path:
                fragment.instruction(
                    OpCode.CALL_VAR_ATTR_NAMES, (str(obj), str(attr), tuple(kw_names), opa))
            else:
                fragment.instruction(
                    OpCode.CALL_ATTR_NAMES, (str(attr), tuple(kw_names), opa, 0))
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
                    fragment.instruction(OpCode.BUILD_ARGS, len(data))
                else:
                    fragment.merge(compiler.full_compile(data, False))
            if len(arg_chunks) > 1:
                fragment.instruction(OpCode.COMBINE_ARGS, len(arg_chunks))

        if kwds_seq:
            kwd_chunks = _partition_kwds(kwds_seq)
            for kind, data in kwd_chunks:
                if kind == "kw":
                    names = tuple(n for n, _ in data)
                    for _, expr in data:
                        fragment.merge(compiler.full_compile(expr, False))
                    fragment.instruction(OpCode.BUILD_KWDS_NAMES, names)
                else:
                    fragment.merge(compiler.full_compile(data, False))
            if len(kwd_chunks) > 1:
                fragment.instruction(OpCode.COMBINE_KWDS, len(kwd_chunks))

        fragment.position_of(source)
        if use_var_path:
            fragment.instruction(
                OpCode.CALL_VAR_ATTR_STAR, (str(obj), str(attr), opa, 0))
        else:
            fragment.instruction(
                OpCode.CALL_ATTR_STAR, (str(attr), opa, 0, 0))

    return fragment


# The end.
