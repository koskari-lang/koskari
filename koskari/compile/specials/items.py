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
from ..helpers import emit_item_access, emit_item_place, feeder_expr
from ..processor import item_arg, item_opcode


_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key")),
]

_SET_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key"), VALUE("val")),
]


@builtin("compile/item")
@special("compile/item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_item(compiler, called_by, obj, key):
    """
    Compile (compile/item OBJ KEY). Load item at KEY from OBJ
    """

    return emit_item_access(
        compiler, called_by, obj, key, OpCode.ITEM, OpCode.VAR_ITEM)


@builtin("compile/del-item")
@special("compile/del-item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_del_item(compiler, called_by, obj, key):
    """
    Compile (compile/del-item OBJ KEY). Delete item at KEY from OBJ
    """

    frag = emit_item_access(
        compiler, called_by, obj, key, OpCode.ITEM_CLEAR, OpCode.VAR_ITEM_CLEAR)
    frag.instruction(OpCode.CONST, None)
    return frag


@builtin("compile/take-item")
@special("compile/take-item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_take_item(compiler, called_by, obj, key):
    """
    Compile (compile/take-item OBJ KEY). Get item at KEY from OBJ, delete it,
    push the value
    """

    return emit_item_access(
        compiler, called_by, obj, key, OpCode.ITEM_TAKE, OpCode.VAR_ITEM_TAKE)


@builtin("compile/set-item")
@special("compile/set-item", patterns=_SET_ITEM_PATTERNS, unpack="kwargs")
def special_set_item(compiler, called_by, obj, key, val):
    """
    Compile (compile/set-item OBJ KEY VAL). Store VAL at item KEY in OBJ
    """

    keyfrag = compiler.full_compile(key.datum)
    valfrag = compiler.full_compile(val.datum)
    key = feeder_expr(keyfrag)
    val_kind, _ = feeder_expr(valfrag)

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    obj_op, key_op, fields = emit_item_place(
        compiler, frag, obj, keyfrag, key, val_kind is not None)
    frag.merge(valfrag)
    if fields:
        frag.instruction(item_opcode(obj_op, key_op, 'STORE'), item_arg(fields))
    else:
        frag.instruction(OpCode.ITEM_STORE, 0)
    frag.instruction(OpCode.CONST, None)
    return frag


# The end.
