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
from ..helpers import emit_item_access, feeder_expr
from ..processor import item_arg, item_opcode


_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key")),
]

_SET_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key"), VALUE("val")),
]


ITEM_FEEDER_OP = {'var': OpCode.VAR, 'const': OpCode.CONST}


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


def emit_item_place(compiler, frag, obj, keyfrag, key_kind, value_feeders):
    """
    Emit the object and key of a store or in-place row, answering the
    feeder opcodes the row names for them, None for a stack operand.
    Plain when the key and every value are feeders: the object and the
    key compile to the stack and the compress pipeline folds the row's
    trailing feeders. Otherwise a non-feeder operand would sit between
    a named operand and the row, so the object is named when it
    compiles to a bare name load (nil is a symbol that does not) and
    the key when a feeder, and only the rest compiles to the stack

    :param compiler: the Compiler
    :param frag: the fragment being built
    :param obj: the object match
    :param keyfrag: the compiled key
    :param key_kind: the key's feeder kind, or None
    :param value_feeders: whether every value is a feeder
    :return: (obj_op, key_op, fields) for item_opcode and item_arg
    """

    objfrag = compiler.full_compile(obj.datum)
    obj_kind, obj_name = feeder_expr(objfrag)
    if obj_kind != 'var':
        obj_name = None
    if (key_kind and value_feeders) or not (obj_name or key_kind):
        frag.merge(objfrag)
        frag.merge(keyfrag)
        return None, None, []

    fields = []
    obj_op = key_op = None
    if obj_name is None:
        frag.merge(objfrag)
    else:
        frag.request_var(obj_name)
        obj_op = OpCode.VAR
        fields.append(obj_name)
    if key_kind is None:
        frag.merge(keyfrag)
    else:
        _, key_val = feeder_expr(keyfrag)
        if key_kind == 'var':
            frag.request_var(key_val)
        key_op = ITEM_FEEDER_OP[key_kind]
        fields.append(key_val)
    return obj_op, key_op, fields


@builtin("compile/set-item")
@special("compile/set-item", patterns=_SET_ITEM_PATTERNS, unpack="kwargs")
def special_set_item(compiler, called_by, obj, key, val):
    """
    Compile (compile/set-item OBJ KEY VAL). Store VAL at item KEY in OBJ
    """

    keyfrag = compiler.full_compile(key.datum)
    valfrag = compiler.full_compile(val.datum)
    key_kind, _ = feeder_expr(keyfrag)
    val_kind, _ = feeder_expr(valfrag)

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    obj_op, key_op, fields = emit_item_place(
        compiler, frag, obj, keyfrag, key_kind, val_kind is not None)
    frag.merge(valfrag)
    if fields:
        frag.instruction(item_opcode(obj_op, key_op, 'STORE'), item_arg(fields))
    else:
        frag.instruction(OpCode.ITEM_STORE, 0)
    frag.instruction(OpCode.CONST, None)
    return frag


# The end.
