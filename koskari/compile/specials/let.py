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
koskari.compile.specials.let

Special form handler for compile/let.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import cons, nil, pair, symbol
from ...types import is_pair
from ..compiler import Compiler
from ..exceptions import CompilerException
from ..helpers import gather_bindings, helper_begin, is_symbolish, pycons, special


@special(symbol("compile/let"))
def special_let(
        compiler: Compiler,
        source: pair,
        tc: bool = False):
    """
    Compile (compile/let BINDINGS BODY...) or (compile/let NAME BINDINGS
    BODY...). Named let expands to an immediately-invoked named lambda:
    (let foo [[a 1] [b 2]] ...body) -> ((lambda foo [a b] ...body) 1 2).
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("let requires bindings and body", source)

    bindings, body = rest

    # TODO: adopt ceval-style named let: bind values as keyword defaults so
    # (let foo [[a 1] [b 2]] body) -> ((lambda foo [a: 1 b: 2] body)); allows
    # re-invocation with partial replacement.
    # Named let: (let NAME [[var val] ...] BODY...) -> ((lambda NAME [var ...] BODY) val ...)
    if is_symbolish(bindings):
        named = bindings
        bindings, body = body

        args, vals = gather_bindings(bindings, source)

        # Build (compile/lambda NAME [formal...] BODY...) using list_star so body_forms
        # is the tail; then build (compile/call LAMBDA val...) using variadic cons.

        src = (symbol('compile/call'),
               (symbol('compile/lambda'), named, cons(*args, nil), *body.unpack()),
               *vals)
        return pycons(src)

    # Unnamed let: (let [[var val] ...] BODY...) — push a child namespace, bind
    # names to compiled values, compile body in that scope, pop.
    args, vals = gather_bindings(bindings, source)
    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    with fragment.child_namespace(called_by, args, vals) as frag:
        helper_begin(frag, body, tc)

    return fragment


# The end.
