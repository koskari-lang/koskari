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
koskari.compile.specials.items

Special form handlers for compile/item, compile/del-item, and compile/set-item.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ...types import is_pair
from ...exceptions import SpecialError
from ...stdlib import builtin
from ..helpers import const_only_expr, special


@builtin("compile/item")
@special(symbol("compile/item"))
def special_item(compiler, source, tc=False):
    """
    Compile (compile/item OBJ KEY). Load item at KEY from OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("item requires two arguments", source)
    obj, (key, rest) = body

    if rest is not nil:
        raise SpecialError("item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM, 0)

    return frag


@builtin("compile/del-item")
@special(symbol("compile/del-item"))
def special_del_item(compiler, source, tc=False):
    """
    Compile (compile/del-item OBJ KEY). Delete item at KEY from OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("del-item requires two arguments", source)
    obj, (key, rest) = body

    if rest is not nil:
        raise SpecialError("del-item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_CLEAR, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM_CLEAR, 0)

    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@builtin("compile/set-item")
@special(symbol("compile/set-item"))
def special_set_item(compiler, source, tc=False):
    """
    Compile (compile/set-item OBJ KEY VAL). Store VAL at item KEY in OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("set-item requires three arguments", source)
    obj, (key, (val, rest)) = body

    if rest is not nil:
        raise SpecialError("set-item takes three arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_STORE, key_val)

    else:
        frag.merge(keyfrag)
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_ITEM_STORE, 0)

    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
