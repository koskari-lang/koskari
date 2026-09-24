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


_ATTR_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), SYMBOL("sym")),
]

_SET_ATTR_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), SYMBOL("sym"), VALUE("val")),
]


def emit_attr(compiler, called_by, obj, sym, var_op, stack_op, before=None):
    """
    A fragment for an attribute special: a symbol object takes the var
    path, var_op with the (obj, sym, 0, 0) operand; any other object is
    compiled onto the stack and takes stack_op with the attribute name.
    before, when given, is called with the fragment between the object
    and the row so a store can compile its value in the right place

    :return: the fragment
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)

    obj = obj.datum
    name = str(sym.datum.base)

    if is_symbol(obj):
        frag.request_var(obj)
        if before is not None:
            before(frag)
        frag.instruction(var_op, (str(obj), name, 0, 0))
    else:
        frag.merge(compiler.full_compile(obj))
        if before is not None:
            before(frag)
        frag.instruction(stack_op, name)

    return frag


@builtin("compile/attr")
@special("compile/attr", patterns=_ATTR_PATTERNS, unpack="kwargs")
def special_attr(compiler, called_by, obj, sym):
    """
    Compile (compile/attr OBJ SYM). Load attribute SYM from OBJ
    """

    return emit_attr(
        compiler, called_by, obj, sym, OpCode.VAR_ATTR, OpCode.ATTR)


@builtin("compile/take-attr")
@special("compile/take-attr", patterns=_ATTR_PATTERNS, unpack="kwargs")
def special_take_attr(compiler, called_by, obj, sym):
    """
    Compile (compile/take-attr OBJ SYM). Get attribute SYM from OBJ, delete
    it, push the value
    """

    return emit_attr(
        compiler, called_by, obj, sym, OpCode.VAR_ATTR_TAKE, OpCode.ATTR_TAKE)


@builtin("compile/del-attr")
@special("compile/del-attr", patterns=_ATTR_PATTERNS, unpack="kwargs")
def special_del_attr(compiler, called_by, obj, sym):
    """
    Compile (compile/del-attr OBJ SYM). Delete attribute SYM from OBJ
    """

    frag = emit_attr(
        compiler, called_by, obj, sym, OpCode.VAR_ATTR_CLEAR, OpCode.ATTR_CLEAR)

    # every expression must evaluate to something
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/set-attr")
@special("compile/set-attr", patterns=_SET_ATTR_PATTERNS, unpack="kwargs")
def special_set_attr(compiler, called_by, obj, sym, val):
    """
    Compile (compile/set-attr OBJ SYM VAL). Store VAL into attribute SYM on OBJ
    """

    frag = emit_attr(
        compiler, called_by, obj, sym, OpCode.VAR_ATTR_STORE, OpCode.ATTR_STORE,
        before=lambda frag: frag.merge(compiler.full_compile(val.datum)))

    # every expression must evaluate to something
    frag.instruction(OpCode.CONST, None)

    return frag


# The end.
