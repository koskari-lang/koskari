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
from ...lib import OpCode, cons, nil, symbol
from ...matcher import CALLEDBY, LIST, LITERAL_KW, MATCH, ONEOF, REST, VALUE
from ...syntax import special
from ...types import is_nil
from ..compiler import ByteCodeFragment, Compiler
from .continuations import refuse_yield


def _compile_segment(compiler, called_by, fragment, src, role):
    """
    Compile a before or after segment as an aside, in the scope the
    dyn-wind is written in, so its names are that scope's. A yield
    written in the segment is refused, since the segment's frame is
    not the generator's; one in a function defined within it belongs
    to that function

    :param compiler: the Compiler
    :param called_by: the special's called-by capture
    :param fragment: the fragment the dyn-wind is emitted into
    :param src: the segment's source
    :param role: 'before' or 'after'
    :return: the Aside
    """

    with fragment.aside(f"<{role}>") as (inner, aside):
        inner.position(called_by.position)
        inner.merge(compiler.full_compile(src))
        refuse_yield(called_by, inner.instructions, f"{role} cannot yield")
    return aside


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
    at least one of before or after is required. Emits PUSH_WIND
    carrying each segment as an aside, with None for an absent one,
    then the body and POP_WIND.
    """

    called_by = caps["called-by"]

    before_src, body_src, after_src = _find_segments(caps, called_by)

    fragment = compiler.fragment(called_by.datum)

    # an absent segment is nil from _find_segments; a falsy literal in
    # a segment is an expression the segment returns
    before = after = None
    if not is_nil(before_src):
        before = _compile_segment(
            compiler, called_by, fragment, before_src, 'before')
    if not is_nil(after_src):
        after = _compile_segment(
            compiler, called_by, fragment, after_src, 'after')

    fragment.position(called_by.position)
    fragment.instruction(OpCode.PUSH_WIND, (before, after, 0, 0))
    fragment.merge(compiler.full_compile(body_src))
    fragment.instruction(OpCode.POP_WIND, 0)
    return fragment


# The end.
