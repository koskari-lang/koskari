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
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...hygiene import function_constant
from ...lib import FormalsSpec, OpCode, cons, nil, symbol
from ...matcher import CALLEDBY, LIST, LITERAL_KW, MATCH, ONEOF, REST, VALUE
from ...syntax import special
from ...types import is_nil
from ..compiler import ByteCodeFragment, Compiler


def _compile_thunk(compiler, body_src, fragment):
    """
    Compile a zero-argument thunk (lambda [] body) and append to fragment.
    Returns the fragment with CONST (FunctionSpec) and BUILD_FUNCTION on
    stack.
    """
    thunk = compiler.fragment("<dyn-wind-thunk>")
    with compiler.resolution_scope(thunk, function=True):
        thunk.merge(compiler.full_compile(body_src))
    fragment.instruction(OpCode.CONST, function_constant(
        compiler, fragment, thunk, None,
        FormalsSpec((), (), (), (), None, None)))
    fragment.instruction(OpCode.BUILD_FUNCTION, ((), (), 0, 0))
    return fragment


def _find_segments(caps, called_by):
    """
    Resolve the before, body, and after segments from one dyn-wind
    match: the positional arms name their segments directly, the
    keyword arms wrap each segment list in a begin
    """

    before = caps.get("before_single")
    body = caps.get("body_single")
    after = caps.get("after_single")

    if body:
        before_src = before.datum if before else nil
        body_src = body.datum
        after_src = after.datum if after else nil

    else:
        before = caps.get("before")
        body = caps.get("body")
        after = caps.get("after")

        before_src = (
            cons(symbol("begin"), before.datum)
            if before else nil)
        body_src = (
            cons(symbol("begin"), body.datum)
            if body else nil)
        after_src = (
            cons(symbol("begin"), after.datum)
            if after else nil)

    if is_nil(before_src) and is_nil(after_src):
        raise SpecialError(
            "dyn-wind requires at least one of before and after",
            called_by.datum, position=called_by.position)

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
@special("compile/dyn-wind", patterns=_DYN_WIND_PATTERNS_FULL,
         unpack="match")
def special_dyn_wind(
        compiler: Compiler,
        caps: dict) -> ByteCodeFragment:
    """
    Compile (compile/dyn-wind [before] [body] [after]). Body is required;
    at least one of before or after is required. Wraps before/after in
    (lambda [] ...) thunks and emits PUSH_WIND / POP_WIND.
    """

    called_by = caps["called-by"]

    before_src, body_src, after_src = _find_segments(caps, called_by)

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)

    if before_src and after_src:
        _compile_thunk(compiler, before_src, fragment)
        _compile_thunk(compiler, after_src, fragment)
        fragment.instruction(OpCode.PUSH_WIND, 2)
    elif before_src:
        _compile_thunk(compiler, before_src, fragment)
        fragment.instruction(OpCode.PUSH_WIND, 1)
    else:
        fragment.instruction(OpCode.CONST, None)
        _compile_thunk(compiler, after_src, fragment)
        fragment.instruction(OpCode.PUSH_WIND, 2)

    fragment.merge(compiler.full_compile(body_src))
    fragment.instruction(OpCode.POP_WIND, 0)
    return fragment


# The end.
