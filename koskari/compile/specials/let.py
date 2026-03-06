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
from ...matcher import BIND_SYM, MATCH, REST, match
from ..compiler import Compiler
from ...exceptions import SpecialError
from ..helpers import BIND_BINDINGS, helper_begin, special, special_forms


_sym_call = symbol('compile/call')
_sym_lambda = symbol('compile/lambda')


_LET_PATTERNS = [
    MATCH(BIND_SYM("name"), BIND_BINDINGS("bindings"), REST("body")),
    MATCH(BIND_BINDINGS("bindings"), REST("body")),
]


@special_forms(_LET_PATTERNS)
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

    matched = match(rest, _LET_PATTERNS)
    if matched is None:
        raise SpecialError("let requires bindings and body", source)

    name = matched.get("name", None)
    args, vals = matched["bindings"]
    body = matched["body"]

    if name:
        return cons(_sym_call,
            cons(_sym_lambda,
                name,
                cons(*args, nil),
                body,
            ),
            *vals,
            nil,
        )

    else:
        fragment = compiler.fragment(called_by)
        fragment.position_of(source)
        with fragment.child_namespace(called_by, args, vals) as frag:
            helper_begin(frag, body, tc)

    return fragment


# The end.
