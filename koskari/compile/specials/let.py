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


from ...exceptions import SpecialError
from ...lib import Datum, OpCode, cons, nil, symbol
from ...matcher import BINDINGS, BODY, MATCH, SYMBOL
from ...stdlib import builtin
from ...syntax import special
from ...types import is_pair
from ..compiler import Compiler
from ..helpers import helper_begin


_sym_call = symbol('compile/call')
_sym_lambda = symbol('compile/lambda')


def _let_bindings_parallel(bindings):
    """
    Parallel name symbols and RHS expressions from ``caps['bindings']``.

    ``BINDINGS`` yields a list of per-cell dicts; each dict holds Datum
    captures under ``name`` and ``expr`` (matcher binding group).
    """

    if bindings is None:
        return [], []

    cells = bindings

    # print("cells", cells)

    names = []
    values = []
    for fr in cells:
        nc = fr.get("name")
        ec = fr.get("expr")
        names.append(nc.datum if nc is not None else None)
        values.append(ec.datum if ec is not None else None)
    return names, values


_LET_PATTERNS = [
    MATCH(
        SYMBOL("_"),
        SYMBOL("name"),
        BINDINGS("bindings", mode="parallel", scope="body"),
        BODY("body")),

    MATCH(
        SYMBOL("_"),
        BINDINGS("bindings", mode="parallel", scope="body"),
        BODY("body")),
]


@builtin("compile/let")
@special("compile/let", patterns=_LET_PATTERNS, unpack="match")
def special_let(
        compiler: Compiler,
        caps,
        tc: bool = False):
    """
    Compile (compile/let BINDINGS BODY...) or (compile/let NAME BINDINGS
    BODY...). Named let expands to an immediately-invoked named lambda:
    (let foo [[a 1] [b 2]] ...body) -> ((lambda foo [a b] ...body) 1 2).
    """

    called_by = caps["_"].datum
    pos = caps["_"].position

    name = caps.get("name")
    if name:
        name = name.datum

    args, vals = _let_bindings_parallel(caps["bindings"])
    # print("args", args)
    # print("vals", vals)

    body = caps["body"].datum

    if name:
        return cons(_sym_call,
                    cons(_sym_lambda,
                         name,
                         cons(*args, nil),
                         body),
                    *vals,
                    nil)

    else:
        fragment = compiler.fragment(called_by)
        fragment.position(pos)
        with fragment.child_namespace(called_by, args, vals) as frag:
            helper_begin(frag, body, tc)

    return fragment


_LETREC_PATTERNS = [
    MATCH(
        SYMBOL("_"),
        BINDINGS("bindings", mode="parallel", scope="body"),
        BODY("body")),
]


@builtin("compile/letrec")
@special("compile/letrec", patterns=_LETREC_PATTERNS, unpack="match")
def special_letrec(
        compiler: Compiler,
        caps,
        tc: bool = False):
    """
    Compile (compile/letrec BINDINGS BODY...). Each name is declared in a
    single child scope without initial values, then each RHS is evaluated and
    stored left-to-right with KOS_OP_VAR_STORE (same effect as compile/setq)
    so lambdas may close over the same bindings before body runs.
    """

    called_by = caps["_"].datum
    pos = caps["_"].position

    args, vals = _let_bindings_parallel(caps["bindings"])
    # print("args", args)
    # print("vals", vals)

    body = caps["body"].datum

    if len(args) != len(vals):
        raise SpecialError("letrec binding name/value mismatch", pos)

    fragment = compiler.fragment(called_by)
    fragment.position(pos)
    with fragment.child_namespace(called_by, [], []) as frag:
        for name, expr in zip(args, vals):

            if is_pair(expr):
                frag.position_of(expr)

            frag.declare_var(str(name))
            frag.merge(compiler.full_compile(expr))
            frag.instruction(OpCode.KOS_OP_VAR_STORE, str(name))
        helper_begin(frag, body, tc)

    return fragment


# The end.
