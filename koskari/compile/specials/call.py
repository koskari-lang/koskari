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


from ...bootstrap import builtin
from ...lib import OpCode, cons, nil, symbol
from ...matcher import BODY, CALLEDBY, LIST, LITERAL_SYM, MATCH, SYMBOL, VALUE
from ...syntax import special
from ...types import fill_position, is_symbol
from ..compiler import ByteCodeFragment, Compiler
from ..helpers import gather_parameters


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



class CallRows(object):
    """
    The terminal rows one call site emits, one per argument shape:
    plain positional, keyword names, and star. Each is a pair of row
    builders indexed by the var path flag (stack path, var path); a
    builder takes the shape's data and returns (opcode, arg). A site
    whose stack-path names row reads the names tuple from the stack
    sets names_const so the emitter pushes it first
    """

    def __init__(self, plain, names, star, names_const=False):
        self.plain = plain
        self.names = names
        self.star = star
        self.names_const = names_const


def emit_call(compiler, fragment, args_src, pos, use_var_path, rows):
    """
    Compile a call's arguments and emit its terminal row. The callee is
    already on the fragment, or named by the var path; args_src is the
    argument list source, and rows the site's CallRows

    :param compiler: the Compiler
    :param fragment: fragment holding the callee so far
    :param args_src: pair list of argument forms, or nil
    :param pos: source position for the terminal row
    :param use_var_path: the callee is a named variable
    :param rows: CallRows for this site
    """

    args_seq, kwds_seq = gather_parameters(args_src, source=args_src)
    has_star = any(x[0] == "*" for x in args_seq)
    has_starstar = any(x[0] == "**" for x in kwds_seq)

    if has_star or has_starstar:
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
                        fragment.merge(compiler.full_compile(expr))
                    fragment.instruction(OpCode.BUILD_ARGS, len(data))
                else:
                    fragment.merge(compiler.full_compile(data))
            if len(arg_chunks) > 1:
                fragment.instruction(OpCode.COMBINE_ARGS, len(arg_chunks))

        if kwds_seq:
            kwd_chunks = _partition_kwds(kwds_seq)
            for kind, data in kwd_chunks:
                if kind == "kw":
                    names = tuple(n for n, _ in data)
                    for _, expr in data:
                        fragment.merge(compiler.full_compile(expr))
                    fragment.instruction(OpCode.BUILD_KWDS_NAMES, names)
                else:
                    fragment.merge(compiler.full_compile(data))
            if len(kwd_chunks) > 1:
                fragment.instruction(OpCode.COMBINE_KWDS, len(kwd_chunks))

        fragment.position(pos)
        fragment.instruction(*rows.star[use_var_path](opa))

    elif kwds_seq:
        kw_names = tuple(name for name, _ in kwds_seq)
        opa = len(args_seq)
        for _, expr in args_seq:
            fragment.merge(compiler.full_compile(expr))
        for _, expr in kwds_seq:
            fragment.merge(compiler.full_compile(expr))
        if not use_var_path and rows.names_const:
            fragment.instruction(OpCode.CONST, kw_names)
        fragment.position(pos)
        fragment.instruction(*rows.names[use_var_path](kw_names, opa))

    else:
        for _, expr in args_seq:
            fragment.merge(compiler.full_compile(expr))
        fragment.position(pos)
        fragment.instruction(*rows.plain[use_var_path](len(args_seq)))


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
        caps: dict) -> ByteCodeFragment:

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

    is_sym = is_symbol(callable)
    name = str(callable) if is_sym else None
    use_var_path = is_sym and compiler.symbol_is_variable(callable)

    # rewriting (.foo obj args...) to
    # (compile/call-attr obj foo args...); the attribute is a label,
    # so a mark on the callable does not reach it
    if name and rest and name.startswith("."):
        obj, rest = rest
        rewritten = cons(
            _sym_call_attr,
            obj, symbol(str(callable.base)[1:]), rest)
        fill_position(rewritten, pos)
        return rewritten

    fragment = compiler.fragment(called_by)
    if use_var_path:
        # the var path names the callable directly rather than routing it
        # through compile_symbol, so the stray-colon check has to run here.
        # The other path calls full_compile, which checks for us.
        compiler.check_stray_colon(callable.base, pos)
        fragment.request_var(callable)

    fragment.position(pos)
    if not use_var_path:
        fragment.merge(compiler.full_compile(callable))

    rows = CallRows(
        plain=(
            lambda n: (OpCode.CALL, n),
            lambda n: (OpCode.CALL_VAR, (callable, n, 0, 0))),
        names=(
            lambda kw, n: (OpCode.CALL_NAMES, n),
            lambda kw, n: (OpCode.CALL_VAR_NAMES, (callable, kw, n, 0))),
        star=(
            lambda opa: (OpCode.CALL_STAR, opa),
            lambda opa: (OpCode.CALL_VAR_STAR, (callable, opa, 0, 0))),
        names_const=True)
    emit_call(compiler, fragment, rest, pos, use_var_path, rows)
    return fragment


_CALL_ATTR_PATTERNS = [
    MATCH(CALLEDBY(), VALUE("obj"), SYMBOL("attr")),
    MATCH(CALLEDBY(), VALUE("obj"), SYMBOL("attr"), BODY("args")),
]


@builtin("compile/call-attr")
@special("compile/call-attr", patterns=_CALL_ATTR_PATTERNS, unpack="match")
def special_call_attr(
        compiler: Compiler,
        caps: dict) -> ByteCodeFragment:

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    obj = caps["obj"].datum
    attr = str(caps["attr"].datum.base)
    args_src = caps["args"].datum if "args" in caps else nil

    fragment = compiler.fragment(called_by)

    # The attr call takes the var path when obj is a symbol that reads a
    # variable; the processor's promotion pass relabels tail-position
    # attr calls onto their TAIL_CALL_*ATTR* twins after emission.
    use_var_path = is_symbol(obj) and compiler.symbol_is_variable(obj)

    fragment.position(pos)
    if use_var_path:
        fragment.request_var(obj)
    else:
        fragment.merge(compiler.full_compile(obj))

    rows = CallRows(
        plain=(
            lambda n: (OpCode.CALL_ATTR, (attr, n, 0, 0)),
            lambda n: (OpCode.CALL_VAR_ATTR, (obj, attr, n, 0))),
        names=(
            lambda kw, n: (OpCode.CALL_ATTR_NAMES, (attr, kw, n, 0)),
            lambda kw, n: (OpCode.CALL_VAR_ATTR_NAMES, (obj, attr, kw, n))),
        star=(
            lambda opa: (OpCode.CALL_ATTR_STAR, (attr, opa, 0, 0)),
            lambda opa: (OpCode.CALL_VAR_ATTR_STAR, (obj, attr, opa, 0))))
    emit_call(compiler, fragment, args_src, pos, use_var_path, rows)
    return fragment


# The end.
