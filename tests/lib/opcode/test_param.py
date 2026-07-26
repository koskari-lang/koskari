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
Tests for the PARAM and PARAM_CONST opcodes

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from koskari.lib import Domain, ParamRef
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestParamOpcodes(CevalTest):
    """
    Tests for PARAM (name plus 0/1 stacked default) and PARAM_CONST
    (name plus const default)
    """

    async def test_param_reads_extent_binding(self):
        """
        PARAM with no default resolves the enclosing parameterize extent
        """

        code, ns = build_expr([
            ["CONST", 42],
            ["PUSH_PARAM", ("x",)],
            ["PARAM", ("x", 0, 0, 0)],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 42)


    async def test_param_no_default_unbound_raises_nameerror(self):
        """
        PARAM at count 0 raises NameError when nothing is bound
        """

        code, ns = build_expr([
            ["PARAM", ("nobody", 0, 0, 0)],
            ["RETURN", 1],
        ])
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_param_stacked_default_answers_on_miss(self):
        """
        PARAM at count 1 leaves the stacked default as the result when
        nothing is bound
        """

        code, ns = build_expr([
            ["CONST", "fallback"],
            ["PARAM", ("absent", 1, 0, 0)],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), "fallback")


    async def test_param_stacked_default_dropped_on_hit(self):
        """
        PARAM at count 1 drops the stacked default when the name resolves,
        leaving exactly one value on the stack
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["PUSH_PARAM", ("y",)],
            ["CONST", "fallback"],
            ["PARAM", ("y", 1, 0, 0)],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ])
        self.assertEqual(await make_callable(code, ns)(), 7)


    async def test_param_const_hit_and_miss(self):
        """
        PARAM_CONST prefers the binding and falls back to its const default
        """

        def seq(*args):
            return args

        code, ns = build_expr([
            ["VAR", "f"],
            ["CONST", 5],
            ["PUSH_PARAM", ("z",)],
            ["PARAM_CONST", ("z", "dflt", 0, 0)],
            ["POP_PARAM", 0],
            ["PARAM_CONST", ("z", "dflt", 0, 0)],
            ["CALL", 2],
            ["RETURN", 1],
        ], values={"f": seq})
        self.assertEqual(await make_callable(code, ns)(), (5, "dflt"))


    async def test_param_reads_root_scope(self):
        """
        With no extent in the way, PARAM resolves the root scope
        """

        def put():
            ParamRef("rooted").store("from-root")

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["POP", 1],
            ["PARAM", ("rooted", 0, 0, 0)],
            ["RETURN", 1],
        ], values={"f": put})
        self.assertEqual(await make_callable(code, ns)(), "from-root")


    async def test_param_miss_creates_no_cell(self):
        """
        A PARAM miss answered by its default writes to neither store
        """

        seen = []

        def inspect():
            seen.append(Domain.current_domain().param_scopes())

        code, ns = build_expr([
            ["CONST", "fallback"],
            ["PARAM", ("ghost", 1, 0, 0)],
            ["POP", 1],
            ["PARAM_CONST", ("phantom", "fallback", 0, 0)],
            ["POP", 1],
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": inspect})
        await make_callable(code, ns)()

        scopes = seen[0]
        for name in ("ghost", "phantom"):
            self.assertNotIn(name, scopes["shadow"])
            self.assertNotIn(name, scopes["root"])


# The end.
