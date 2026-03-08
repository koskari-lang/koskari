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
from ...lib import (FormalsSpec, FunctionSpec, OpCode, nil, pair,
                    symbol)
from ...types import is_pair
from ...matcher import ANY_ORDER, LITERAL_KW, MATCH, match, VALUE
from ..compiler import ByteCodeFragment, Compiler, CompilerMode
from ..helpers import helper_begin, special, special_forms


def _compile_thunk(compiler, body_src, fragment):
    """
    Compile a zero-argument thunk (lambda [] body) and append to fragment.
    Returns the fragment with CONST (FunctionSpec) and BUILD_FUNCTION 0 on stack.
    """
    thunk = compiler.fragment("<dyn-wind-thunk>")
    thunk = helper_begin(thunk, body_src, True)
    code = compiler.complete(thunk, CompilerMode.FUNCTION)
    fspec = FunctionSpec(None, FormalsSpec((), (), (), (), None, None), code)
    fragment.instruction(OpCode.KOS_OP_CONST, fspec)
    fragment.instruction(OpCode.KOS_OP_BUILD_FUNCTION, 0)
    return fragment


_POSITIONAL_3 = MATCH(
    VALUE("before"),
    VALUE("body"),
    VALUE("after"),
)
_POSITIONAL_2 = MATCH(
    VALUE("body"),
    VALUE("after"),
)
_KEYWORD_ALL = MATCH(ANY_ORDER(
    MATCH(LITERAL_KW("before:"), VALUE("before")),
    MATCH(LITERAL_KW("body:"), VALUE("body")),
    MATCH(LITERAL_KW("after:"), VALUE("after")),
))
_KEYWORD_BEFORE_BODY = MATCH(ANY_ORDER(
    MATCH(LITERAL_KW("before:"), VALUE("before")),
    MATCH(LITERAL_KW("body:"), VALUE("body")),
))
_KEYWORD_BODY_AFTER = MATCH(ANY_ORDER(
    MATCH(LITERAL_KW("body:"), VALUE("body")),
    MATCH(LITERAL_KW("after:"), VALUE("after")),
))
_DYN_WIND_PATTERNS = [
    _KEYWORD_ALL,
    _KEYWORD_BEFORE_BODY,
    _KEYWORD_BODY_AFTER,
    _POSITIONAL_3,
    _POSITIONAL_2,
]


def _find_segments(source):
    """
    Find the before, body, and after segments in the source.
    """

    matched = match(source, _DYN_WIND_PATTERNS)
    if matched is None:
        raise SpecialError(
            "dyn-wind requires before, body, and after segments",
            source)

    before_src = matched.get("before")
    before_src = before_src.source if before_src else nil

    body_src = matched["body"].source

    after_src = matched.get("after")
    after_src = after_src.source if after_src else nil

    if before_src is nil and after_src is nil:
        raise SpecialError(
            "dyn-wind requires at least one of before, body, and after",
            source)

    return (before_src, body_src, after_src)


@special_forms(_DYN_WIND_PATTERNS)
@special(symbol("compile/dyn-wind"))
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
    if rest is nil or not is_pair(rest):
        raise SpecialError("dyn-wind requires before, body, and after", source)

    before_src, body_src, after_src = _find_segments(rest)

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

    helper_begin(fragment, body_src, False)
    fragment.instruction(OpCode.KOS_OP_POP_WIND, 0)
    return fragment


# The end.
