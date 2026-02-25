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
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, pair, symbol
from ...types import is_pair
from ..compiler import ByteCodeFragment, Compiler
from ...exceptions import SpecialError
from ..helpers import gather_bindings, helper_begin, helper_binding, special


@special(symbol("compile/parameterize"))
def special_parameterize(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (compile/parameterize [[name val] ...] BODY...). Binds names as
    dynamic parameters for the duration of body; restores ambient values on
    exit. Uses PUSH_PARAM (values on stack, names tuple constant) and POP_PARAM.
    Consumers of parameters (e.g. functions that would use bind-param) read the
    current value via ParamRef(name).deref(); the body typically calls such
    consumers. See tests/compile/specials/test_parameterize.py.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError(
            "parameterize requires bindings and body",
            source)

    bindings, body = rest
    args, vals = gather_bindings(bindings, source)
    fragment = compiler.fragment(called_by)
    fragment.position_of(source)

    for val in vals:
        fragment.merge(compiler.full_compile(val, False))

    fragment.instruction(OpCode.KOS_OP_PUSH_PARAM, tuple(map(str, args)))
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_PARAM, 0)
    return fragment


@special(symbol("compile/with"))
def special_with(compiler, source, tc=False):
    """
    Compile (compile/with [name? expr] BODY...). Single-shot context manager
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("with requires [name? expr] and body", source)

    spec, body = rest
    if spec is nil or not is_pair(spec):
        raise SpecialError("with requires [name? expr] list", source)

    name, expr = helper_binding(spec, allow_unnamed=True)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.merge(compiler.full_compile(expr, False))

    if name is not None:
        name = str(name)
        fragment.declare_var(name)

    fragment.instruction(OpCode.KOS_OP_PUSH_WITH, name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_WITH, 0)

    return fragment


@special(symbol("compile/dyn-with"))
def special_dyn_with(compiler, source, tc=False):
    """
    Compile (compile/dyn-with [name? expr] BODY...). Multi-shot context manager
    that participates in transfer plans (exit on unwind, enter on wind).
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("dyn-with requires [name? expr] and body", source)

    spec, body = rest
    if spec is nil or not is_pair(spec):
        raise SpecialError("dyn-with requires [name? expr] list", source)

    name, expr = helper_binding(spec, allow_unnamed=True)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.merge(compiler.full_compile(expr, False))

    if name is not None:
        name = str(name)
        fragment.declare_var(name)

    fragment.instruction(OpCode.KOS_OP_PUSH_DYN_WITH, name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_DYN_WITH, 0)

    return fragment


# The end.
