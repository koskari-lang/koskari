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
koskari.compile.specials.thunk

Special form compile/gensym-thunk: placeholder-driven thunk lambda sugar.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import re
from ...exceptions import SpecialError
from ...lib import cons, keyword, nil, symbol, gensym
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...types import is_nil, is_pair, is_symbol
from ...syntax import special
from ..compiler import ByteCodeFragment, Compiler


_keyword_star = keyword("*")
_monitor = (symbol("gensym-thunk"),)


_thunksym = re.compile("^%[1-9][0-9]*$").match

_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


def _transform(pos_map, form):
    """
    Copy form with %N and Ellipsis rewritten; skip nested gensym-thunk forms
    """

    if is_nil(form):
        return form

    result = nil
    end = nil

    def append(obj):
        nonlocal result
        nonlocal end

        if is_nil(result):
            result = obj
        else:
            end[1] = obj
        end = obj

    while not is_nil(form):
        head, tail = form

        if is_nil(head):
            val = cons(nil, nil)

        elif is_pair(head):
            if head[0] in _monitor:
                # don't descend into gensym-thunk forms
                val = cons(head, nil)
            else:
                val = cons(_transform(pos_map, head), nil)

        elif is_symbol(head):
            if _thunksym(str(head)):
                if head in pos_map:
                    head = pos_map[head]
                else:
                    pos_map[head] = gensym(str(head))
                    head = pos_map[head]
            val = cons(head, nil)

        elif head is Ellipsis:
            if head in pos_map:
                head = pos_map[head]
            else:
                pos_map[head] = gensym("star")
                head = pos_map[head]

            append(cons(_keyword_star, nil))
            val = cons(head, nil)

        else:
            val = cons(head, nil)

        val.set_position(form.get_position())
        append(val)
        form = tail

    return result


def _build_formals(pos_map, source):
    """
    Build the [formals...] list for compile/lambda (proper list ending in nil)
    """

    star = pos_map.pop(Ellipsis, None)

    keysyms = sorted(pos_map.keys(), key=lambda k: int(str(k).strip("%")))
    if keysyms:
        keys = [int(str(k).strip("%")) for k in keysyms]
        if keys[0] != 1 or keys[-1] != len(keys):
            raise SpecialError(
                "gensym-thunk placeholders must be contiguous"
                f" from %1 to %{len(keys)}",
                source)

    if not keysyms and not star:
        return nil
    elif star:
        return cons(*[pos_map[k] for k in keysyms], _keyword_star, star, nil)
    else:
        return cons(*[pos_map[k] for k in keysyms], nil)


@builtin("compile/gensym-thunk")
@special("compile/gensym-thunk", patterns=_DEFAULT_PATTERNS)
def special_gensym_thunk(
        compiler: Compiler,
        source,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (compile/gensym-thunk BODY...) to a lambda with inferred formals.

    This is the form which handles thunk-expressions.

    Placeholders: %%1, %%2, ... share gensyms per index; ... becomes * rest
    """

    called_by, rest = source

    pos_map = {}
    if not is_nil(rest):
        rest = _transform(pos_map, rest)
    formals = _build_formals(pos_map, source)

    return cons(symbol("lambda"), formals, rest)


# The end.
