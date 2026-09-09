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

Special form handlers for compile/attr, compile/del-attr, compile/take-attr,
and compile/set-attr.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, SYMBOL, VALUE
from ...syntax import special
from ...types import is_symbol


_ATTR_FORMS = [
    MATCH(CALLEDBY(), VALUE('obj'), SYMBOL('sym')),
]


@builtin("compile/attr")
@special("compile/attr", patterns=_ATTR_FORMS, unpack="match")
def special_attr(compiler, caps):
    """
    Compile (compile/attr OBJ SYM). Load attribute SYM from OBJ
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    obj = caps["obj"].datum
    sym = caps["sym"].datum

    frag = compiler.fragment(called_by)
    frag.position(pos)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.instruction(OpCode.VAR_ATTR, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.instruction(OpCode.ATTR, str(sym))

    return frag


_DEL_ATTR_FORMS = [
    MATCH(CALLEDBY(), VALUE('obj'), SYMBOL('sym')),
]


@builtin("compile/take-attr")
@special("compile/take-attr", patterns=_DEL_ATTR_FORMS, unpack="match")
def special_take_attr(compiler, caps):
    """
    Compile (compile/take-attr OBJ SYM). Get attribute SYM from OBJ, delete
    it, push the value
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    obj = caps["obj"].datum
    sym = caps["sym"].datum

    frag = compiler.fragment(called_by)
    frag.position(pos)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.instruction(
            OpCode.VAR_ATTR_TAKE, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.instruction(OpCode.ATTR_TAKE, str(sym))

    return frag


@builtin("compile/del-attr")
@special("compile/del-attr", patterns=_DEL_ATTR_FORMS, unpack="match")
def special_del_attr(compiler, caps):
    """
    Compile (compile/del-attr OBJ SYM). Delete attribute SYM from OBJ
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    obj = caps["obj"].datum
    sym = caps["sym"].datum

    frag = compiler.fragment(called_by)
    frag.position(pos)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.instruction(OpCode.VAR_ATTR_CLEAR, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.instruction(OpCode.ATTR_CLEAR, str(sym))

    # every expression must evaluate to something
    frag.instruction(OpCode.CONST, None)

    return frag


_SET_ATTR_FORMS = [
    MATCH(CALLEDBY(), VALUE('obj'), SYMBOL('sym'), VALUE('val')),
]


@builtin("compile/set-attr")
@special("compile/set-attr", patterns=_SET_ATTR_FORMS, unpack="match")
def special_set_attr(compiler, caps):
    """
    Compile (compile/set-attr OBJ SYM VAL). Store VAL into attribute SYM on OBJ
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    obj = caps["obj"].datum
    sym = caps["sym"].datum
    val = caps["val"].datum

    frag = compiler.fragment(called_by)
    frag.position(pos)

    if is_symbol(obj):
        frag.request_var(str(obj))
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.VAR_ATTR_STORE, (str(obj), str(sym), 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.ATTR_STORE, str(sym))

    # every expression must evaluate to something
    frag.instruction(OpCode.CONST, None)

    return frag


# The end.
