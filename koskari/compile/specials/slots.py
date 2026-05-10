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
koskari.compile.specials.slots

Special form handlers for compile/attr, compile/del-attr, and compile/set-attr.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import OpCode, symbol
from ...types import is_symbol
from ...exceptions import SpecialError
from ...stdlib import builtin
from ..helpers import special, special_forms
from ...matcher import MATCH, VALUE, SYMBOL, match


_ATTR_FORMS = [
    MATCH(VALUE('obj'), SYMBOL('sym')),
]


@builtin("compile/attr")
@special_forms(_ATTR_FORMS)
@special(symbol("compile/attr"))
def special_attr(compiler, source, tc=False):
    """
    Compile (compile/attr OBJ SYM). Load attribute SYM from OBJ
    """

    called_by, body = source

    m = match(body, _ATTR_FORMS)
    if m is None:
        raise SpecialError("compile/attr requires OBJ SYM", source)

    obj = m.get('obj').source
    sym = m.get('sym').source

    frag = compiler.fragment(called_by)
    frag.position_of(body)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.instruction(OpCode.KOS_OP_VAR_ATTR, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.instruction(OpCode.KOS_OP_ATTR, str(sym))

    return frag


_DEL_ATTR_FORMS = [
    MATCH(VALUE('obj'), SYMBOL('sym')),
]


@builtin("compile/del-attr")
@special_forms(_DEL_ATTR_FORMS)
@special(symbol("compile/del-attr"))
def special_del_attr(compiler, source, tc=False):
    """
    Compile (compile/del-attr OBJ SYM). Delete attribute SYM from OBJ
    """

    called_by, body = source

    m = match(body, _DEL_ATTR_FORMS)
    if m is None:
        raise SpecialError("compile/del-attr requires OBJ SYM", source)

    obj = m.get('obj').source
    sym = m.get('sym').source

    frag = compiler.fragment(called_by)
    frag.position_of(body)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.instruction(OpCode.KOS_OP_VAR_ATTR_CLEAR, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.instruction(OpCode.KOS_OP_ATTR_CLEAR, str(sym))

    # every expression must evaluate to something
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


_SET_ATTR_FORMS = [
    MATCH(VALUE('obj'), SYMBOL('sym'), VALUE('val')),
]


@builtin("compile/set-attr")
@special_forms(_SET_ATTR_FORMS)
@special(symbol("compile/set-attr"))
def special_set_attr(compiler, source, tc=False):
    """
    Compile (compile/set-attr OBJ SYM VAL). Store VAL into attribute SYM on OBJ
    """

    called_by, body = source

    m = match(body, _SET_ATTR_FORMS)
    if m is None:
        raise SpecialError("compile/set-attr requires OBJ SYM VAL", source)

    obj = m.get('obj').source
    sym = m.get('sym').source
    val = m.get('val').source

    frag = compiler.fragment(called_by)
    frag.position_of(body)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_VAR_ATTR_STORE, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_ATTR_STORE, str(sym))

    # every expression must evaluate to something
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
