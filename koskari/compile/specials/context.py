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


from ...exceptions import SpecialError
from ...lib import OpCode, pair
from ...matcher import BODY, CALLEDBY, MATCH, REST, USING
from ...bootstrap import builtin
from ...syntax import special
from ...types import is_nil, is_pair
from ..compiler import ByteCodeFragment, Compiler
from ..helpers import gather_bindings, helper_begin


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/parameterize")
@special("compile/parameterize", patterns=_DEFAULT_PATTERNS)
def special_parameterize(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile (compile/parameterize [[name val] ...] BODY...).

    Sets named dynamic parameters for the duration of body; restores ambient
    values on exit. Consumers can use these parameters via `define-parameter`
    or `parameterq`
    """

    called_by, rest = source
    if is_nil(rest) or not is_pair(rest):
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


_WITH_PATTERNS = [
    MATCH(CALLEDBY(), USING("binding", scope="body"), BODY("body")),
]


@builtin("compile/with")
@special("compile/with", patterns=_WITH_PATTERNS, unpack="match")
def special_with(compiler, caps, tc=False):
    """
    Compile (compile/with [name? expr] BODY...). Single-shot context manager
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    binding = caps["binding"]

    nc = binding.get("name")
    ec = binding.get("expr")

    name = nc.datum if nc else None
    expr = ec.datum if ec else None

    body = caps["body"].datum

    # print(f"binding: {binding}")
    # print(f"name: {name}")
    # print(f"expr: {expr}")
    # print(f"body: {body}")

    fragment = compiler.fragment(called_by)
    fragment.merge(compiler.full_compile(expr, False))

    if name is not None:
        name = str(name)
        fragment.declare_var(name)

    fragment.position(pos)
    fragment.instruction(OpCode.KOS_OP_PUSH_WITH, name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_WITH, 0)

    return fragment


@builtin("compile/dyn-with")
@special("compile/dyn-with", patterns=_WITH_PATTERNS, unpack="match")
def special_dyn_with(compiler, caps, tc=False):
    """
    Compile (compile/dyn-with [name? expr] BODY...). Multi-shot context manager
    that participates in transfer plans (exit on unwind, enter on wind).
    """

    called_by = caps["called-by"].datum
    pos = caps["called-by"].position

    binding = caps["binding"]

    nc = binding.get("name")
    ec = binding.get("expr")
    name = nc.datum if nc is not None else None
    expr = ec.datum if ec is not None else None

    body = caps["body"].datum

    fragment = compiler.fragment(called_by)
    fragment.merge(compiler.full_compile(expr, False))

    if name is not None:
        name = str(name)
        fragment.declare_var(name)

    fragment.position(pos)
    fragment.instruction(OpCode.KOS_OP_PUSH_DYN_WITH, name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_DYN_WITH, 0)

    return fragment


# The end.
