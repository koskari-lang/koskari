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
from ...lib import (FormalsSpec, FunctionSpec, OpCode, keyword, nil, pair,
                    symbol)
from ...types import is_keyword, is_pair, is_nil, unpack
from ..compiler import ByteCodeFragment, Compiler, CompilerMode
from ..helpers import helper_begin, special


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


_kw_before = keyword('before:')
_kw_body = keyword('body:')
_kw_after = keyword('after:')
_kw_segments = (_kw_before, _kw_body, _kw_after)


def _find_segments(source):
    """
    Find the before, body, and after segments in the source.
    """

    found = []
    seen = set()

    for elem in unpack(source):
        if not is_pair(elem):
            raise SpecialError(
                "dyn-wind segment must be a pair, not %r" % elem,
                source)

        if not is_nil(elem):
            head, tail = elem
            if head in _kw_segments:
                if head in seen:
                    raise SpecialError("duplicate segment", source)
                seen.add(head)
                found.append((head, tail))
            elif is_keyword(head):
                raise SpecialError("unknown dyn-wind segment keyword %r", head)
            else:
                found.append((None, elem))
        else:
            found.append((None, elem))

    if len(found) > 3:
        raise SpecialError("too many dyn-wind segments", source)

    if len(found) < 2:
        raise SpecialError("dyn-wind requires at least one of before, body, and after", source)

    if len(found) == 2:
        if found[0][0] in (None, _kw_body):
            if found[1][0] in (None, _kw_after):
                return [nil, found[0][1], found[1][1]]
            else:
                raise SpecialError("misplaced dyn-wind segment keyword %r", found[1][0])
        elif found[0][0] is not _kw_before:
            raise SpecialError("misplaced dyn-wind segment keyword %r", found[0][0])
        else:
            if found[1][0] in (None, _kw_body):
                return [found[0][1], found[1][1], nil]
            else:
                raise SpecialError("misplaced dyn-wind segment keyword %r", found[1][0])

    return found[0][1], found[1][1], found[2][1]


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
