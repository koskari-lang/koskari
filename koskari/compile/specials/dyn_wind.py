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
koskari.compile.specials.dyn_wind

Special form handler for compile/dyn-wind (dynamic-wind).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...exceptions import SpecialError
from ...lib import (FormalsSpec, FunctionSpec, OpCode, cons, nil, pair,
                    symbol)
from ...types import is_nil, is_pair
from ...matcher import (
    CALLEDBY,
    LITERAL_KW,
    LIST,
    MATCH,
    ONEOF,
    REST,
    VALUE,
)
from ..compiler import ByteCodeFragment, Compiler, CompilerMode
from ...bootstrap import builtin
from ...syntax import special
from ..helpers import match


def _compile_thunk(compiler, body_src, fragment):
    """
    Compile a zero-argument thunk (lambda [] body) and append to fragment.
    Returns the fragment with CONST (FunctionSpec) and BUILD_FUNCTION 0 on stack.
    """
    thunk = compiler.fragment("<dyn-wind-thunk>")
    thunk.merge(compiler.full_compile(body_src, True))
    code = compiler.complete(thunk, CompilerMode.FUNCTION)
    fspec = FunctionSpec(None, FormalsSpec((), (), (), (), None, None), code)
    fragment.instruction(OpCode.KOS_OP_CONST, fspec)
    fragment.instruction(OpCode.KOS_OP_BUILD_FUNCTION, 0)
    return fragment


def _find_segments(source):
    """
    Find the before, body, and after segments in the source.
    """

    matched = match(source, _DYN_WIND_PATTERNS)
    if matched is None:
        raise SpecialError(
            "dyn-wind requires before, body, and after segments",
            source)

    before = matched.get("before_single")
    body = matched.get("body_single")
    after = matched.get("after_single")

    if body:
        before_src = before.datum if before else nil
        body_src = body.datum
        after_src = after.datum if after else nil

    else:
        before = matched.get("before")
        body = matched.get("body")
        after = matched.get("after")

        before_src = (
            cons(symbol("compile/begin"), before.datum)
            if before else nil)
        body_src = (
            cons(symbol("compile/begin"), body.datum)
            if body else nil)
        after_src = (
            cons(symbol("compile/begin"), after.datum)
            if after else nil)

    if is_nil(before_src) and is_nil(after_src):
        raise SpecialError(
            "dyn-wind requires at least one of before, body, and after",
            source)

    return (before_src, body_src, after_src)


_POSITIONAL_3 = MATCH(
    VALUE("before_single"),
    VALUE("body_single"),
    VALUE("after_single"),
)
_POSITIONAL_2 = MATCH(
    VALUE("body_single"),
    VALUE("after_single"),
)
_KW_BEFORE = LIST(LITERAL_KW("before:"), REST("before"))
_KW_BODY = LIST(LITERAL_KW("body:"), REST("body"))
_KW_AFTER = LIST(LITERAL_KW("after:"), REST("after"))
_KEYWORD_ALL = ONEOF(
    MATCH(_KW_BEFORE, _KW_BODY, _KW_AFTER),
    MATCH(_KW_BEFORE, _KW_AFTER, _KW_BODY),
    MATCH(_KW_BODY, _KW_BEFORE, _KW_AFTER),
    MATCH(_KW_BODY, _KW_AFTER, _KW_BEFORE),
    MATCH(_KW_AFTER, _KW_BEFORE, _KW_BODY),
    MATCH(_KW_AFTER, _KW_BODY, _KW_BEFORE),
)
_KEYWORD_BEFORE_BODY = ONEOF(
    MATCH(_KW_BEFORE, _KW_BODY),
    MATCH(_KW_BODY, _KW_BEFORE),
)
_KEYWORD_BODY_AFTER = ONEOF(
    MATCH(_KW_BODY, _KW_AFTER),
    MATCH(_KW_AFTER, _KW_BODY),
)
_DYN_WIND_PATTERNS = [
    _KEYWORD_ALL,
    _KEYWORD_BEFORE_BODY,
    _KEYWORD_BODY_AFTER,
    _POSITIONAL_3,
    _POSITIONAL_2,
]

_DYN_WIND_PATTERNS_FULL = tuple(
    MATCH(CALLEDBY(), m)
    for m in _DYN_WIND_PATTERNS)


@builtin("compile/dyn-wind")
@special("compile/dyn-wind", patterns=_DYN_WIND_PATTERNS_FULL)
def special_dyn_wind(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (compile/dyn-wind [before] [body] [after]). Body is required;
    at least one of before or after is required. Wraps before/after in
    (compile/lambda [] ...) thunks and emits PUSH_WIND / POP_WIND.
    """

    called_by, rest = source
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError("dyn-wind requires before, body, and after", source)

    before_src, body_src, after_src = _find_segments(rest)

    # print(f"before_src: {before_src}")
    # print(f"body_src: {body_src}")
    # print(f"after_src: {after_src}")

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)

    if before_src and after_src:
        _compile_thunk(compiler, before_src, fragment)
        _compile_thunk(compiler, after_src, fragment)
        fragment.instruction(OpCode.KOS_OP_PUSH_WIND, 2)
    elif before_src:
        _compile_thunk(compiler, before_src, fragment)
        fragment.instruction(OpCode.KOS_OP_PUSH_WIND, 1)
    else:
        fragment.instruction(OpCode.KOS_OP_CONST, None)
        _compile_thunk(compiler, after_src, fragment)
        fragment.instruction(OpCode.KOS_OP_PUSH_WIND, 2)

    # helper_begin(fragment, body_src, False)
    fragment.merge(compiler.full_compile(body_src, False))
    fragment.instruction(OpCode.KOS_OP_POP_WIND, 0)
    return fragment


# The end.
