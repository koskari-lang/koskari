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
koskari.compile.specials.context

Special form handlers for compile/parameterize, compile/with, and
compile/dyn-with.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode
from ...matcher import BODY, CALLEDBY, MATCH, SYMBOL, USING, VALUE
from ...syntax import special
from ..helpers import const_only_expr, gather_bindings, helper_begin


_PARAMETERIZE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("bindings"), BODY("body")),
]


@builtin("compile/parameterize")
@special("compile/parameterize", patterns=_PARAMETERIZE_PATTERNS,
         unpack="kwargs")
def special_parameterize(compiler, called_by, bindings, body):
    """
    Compile (compile/parameterize [[name val] ...] BODY...).

    Sets named dynamic parameters for the duration of body; restores ambient
    values on exit. Consumers can use these parameters via `define-parameter`
    or `parameterq`
    """

    args, vals = gather_bindings(bindings.datum, bindings.datum)
    if not args:
        raise SpecialError(
            "parameterize requires at least one binding",
            bindings.datum)

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)

    for val in vals:
        fragment.merge(compiler.full_compile(val))

    fragment.instruction(OpCode.PUSH_PARAM, tuple(map(str, args)))
    helper_begin(fragment, body.datum)
    fragment.instruction(OpCode.POP_PARAM, 0)
    return fragment


_PARAMETER_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("name")),
    MATCH(CALLEDBY("called_by"), SYMBOL("name"), VALUE("default")),
]


@builtin("compile/parameter")
@special("compile/parameter", patterns=_PARAMETER_PATTERNS, unpack="match")
def special_parameter(compiler, caps):
    """
    Compile (compile/parameter NAME [DEFAULT]).

    Resolves the value bound to NAME: the enclosing `parameterize` extent
    first, then the root scope. Neither store is written, and neither
    gains an entry on a miss. Without a binding DEFAULT answers, and with
    no DEFAULT the resolution raises NameError.
    """

    called_by = caps["called_by"]
    name = str(caps["name"].datum)

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)

    if "default" not in caps:
        fragment.instruction(OpCode.PARAM, (name, 0, 0, 0))
        return fragment

    defaultfrag = compiler.full_compile(caps["default"].datum)
    is_const, const_val = const_only_expr(defaultfrag)

    if is_const:
        fragment.instruction(OpCode.PARAM_CONST, (name, const_val, 0, 0))

    else:
        fragment.merge(defaultfrag)
        fragment.instruction(OpCode.PARAM, (name, 1, 0, 0))

    return fragment


_WITH_PATTERNS = [
    MATCH(CALLEDBY("called_by"), USING("binding"), BODY("body")),
]


def emit_with(compiler, called_by, binding, body, push_op, pop_op):
    """
    A fragment for a context-manager special: the manager expression
    compiled, its optional name declared, the body between push_op and
    pop_op

    :return: the fragment
    """

    nc = binding.get("name")
    ec = binding.get("expr")
    name = str(nc.datum) if nc is not None else None
    expr = ec.datum if ec is not None else None

    fragment = compiler.fragment(called_by.datum)
    fragment.merge(compiler.full_compile(expr))

    if name is not None:
        fragment.declare_var(name)

    fragment.position(called_by.position)
    fragment.instruction(push_op, name)
    helper_begin(fragment, body.datum)
    fragment.instruction(pop_op, 0)

    return fragment


@builtin("compile/with")
@special("compile/with", patterns=_WITH_PATTERNS, unpack="kwargs")
def special_with(compiler, called_by, binding, body):
    """
    Compile (compile/with [name? expr] BODY...). Single-shot context manager
    """

    return emit_with(
        compiler, called_by, binding, body, OpCode.PUSH_WITH, OpCode.POP_WITH)


@builtin("compile/dyn-with")
@special("compile/dyn-with", patterns=_WITH_PATTERNS, unpack="kwargs")
def special_dyn_with(compiler, called_by, binding, body):
    """
    Compile (compile/dyn-with [name? expr] BODY...). Multi-shot context manager
    that participates in transfer plans (exit on unwind, enter on wind).
    """

    return emit_with(
        compiler, called_by, binding, body,
        OpCode.PUSH_DYN_WITH, OpCode.POP_DYN_WITH)


# The end.
