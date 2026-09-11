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
"""


from ...bootstrap import builtin
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, VALUE
from ...syntax import special
from ..helpers import emit_item_key


_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key")),
]

_SET_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key"), VALUE("val")),
]


def item_fragment(compiler, called_by, obj):
    """
    A fragment for an item special with its object compiled, positioned
    at the object
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    frag.merge(compiler.full_compile(obj.datum))
    return frag


@builtin("compile/item")
@special("compile/item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_item(compiler, called_by, obj, key):
    """
    Compile (compile/item OBJ KEY). Load item at KEY from OBJ
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(compiler, frag, key.datum, OpCode.CONST_ITEM, OpCode.ITEM)
    return frag


@builtin("compile/del-item")
@special("compile/del-item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_del_item(compiler, called_by, obj, key):
    """
    Compile (compile/del-item OBJ KEY). Delete item at KEY from OBJ
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(
        compiler, frag, key.datum, OpCode.CONST_ITEM_CLEAR, OpCode.ITEM_CLEAR)
    frag.instruction(OpCode.CONST, None)
    return frag


@builtin("compile/take-item")
@special("compile/take-item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_take_item(compiler, called_by, obj, key):
    """
    Compile (compile/take-item OBJ KEY). Get item at KEY from OBJ, delete it,
    push the value
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(
        compiler, frag, key.datum, OpCode.CONST_ITEM_TAKE, OpCode.ITEM_TAKE)
    return frag


@builtin("compile/set-item")
@special("compile/set-item", patterns=_SET_ITEM_PATTERNS, unpack="kwargs")
def special_set_item(compiler, called_by, obj, key, val):
    """
    Compile (compile/set-item OBJ KEY VAL). Store VAL at item KEY in OBJ
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(
        compiler, frag, key.datum, OpCode.CONST_ITEM_STORE, OpCode.ITEM_STORE,
        before=lambda: frag.merge(compiler.full_compile(val.datum)))
    frag.instruction(OpCode.CONST, None)
    return frag


# The end.
