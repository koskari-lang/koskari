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
koskari.compile.specials.refs

Special form handlers for refs: compile/deref, compile/ref-attr,
compile/ref-item, compile/refq, and compile/bindq.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...lib import OpCode
from ...matcher import CALLEDBY, DEFINE, MATCH, SYMBOL, VALUE
from ...syntax import special
from ...types import is_pair
from ..helpers import emit_item_access


_ONE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("arg")),
]

_REF_ATTR_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), SYMBOL("sym")),
]

_REF_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key")),
]

_REFQ_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("sym")),
]


@builtin("compile/deref")
@special("compile/deref", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_deref(compiler, called_by, arg):
    """
    Compile (compile/deref EXPR). Compile EXPR (produces ref on stack),
    then emit DEREF to call ref.deref() and push result.
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(arg.position)
    frag.merge(compiler.full_compile(arg.datum))
    frag.instruction(OpCode.DEREF, 0)

    return frag


@builtin("compile/ref-attr")
@special("compile/ref-attr", patterns=_REF_ATTR_PATTERNS, unpack="kwargs")
def special_ref_attr(compiler, called_by, obj, sym):
    """
    Compile (compile/ref-attr OBJ SYM). Produce a mutable reference to attribute SYM on OBJ
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    frag.merge(compiler.full_compile(obj.datum))
    frag.instruction(OpCode.ATTR_REF, str(sym.datum.base))

    return frag


@builtin("compile/ref-item")
@special("compile/ref-item", patterns=_REF_ITEM_PATTERNS, unpack="kwargs")
def special_ref_item(compiler, called_by, obj, key):
    """
    Compile (compile/ref-item OBJ KEY). Produce a mutable reference to item at KEY in OBJ
    """

    return emit_item_access(
        compiler, called_by, obj, key, OpCode.ITEM_REF, OpCode.VAR_ITEM_REF)


@builtin("compile/refq")
@special("compile/refq", patterns=_REFQ_PATTERNS, unpack="kwargs")
def special_refq(compiler, called_by, sym):
    """
    Compile (compile/refq SYM). Load the value of the binding named SYM
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(sym.position)
    frag.request_var(sym.datum, position=sym.position)
    frag.instruction(OpCode.VAR_REF, sym.datum)

    return frag


_BINDQ_PATTERNS = [
    MATCH(CALLEDBY(), DEFINE("name"), VALUE("value")),
]


@builtin("compile/bindq")
@special("compile/bindq", patterns=_BINDQ_PATTERNS, unpack="match")
def special_bindq(compiler, caps):
    """
    Compile (compile/bindq SYM EXPR). Declare a new binding SYM and initialize
    it to EXPR
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    namesym = caps["name"].datum

    value = caps["value"]
    value_datum = value.datum

    frag = compiler.fragment(called_by)
    frag.declare_var(namesym)

    if not is_pair(value_datum):
        frag.position(value.position)
    frag.merge(compiler.full_compile(value_datum))

    frag.position(pos)
    frag.instruction(OpCode.VAR_BIND, namesym)
    frag.instruction(OpCode.CONST, None)

    return frag


# The end.
