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
Tests for PUSH_COLLECT, POP_COLLECT, BUILD_CLASS opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import pytest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr


pytestmark = pytest.mark.phase_lib


class TestPushCollectPopCollectBuildClass(CevalTest):

    async def test_push_collect_pop_collect_build_class_defines_class(self):
        """
        PUSH_COLLECT / POP_COLLECT / BUILD_CLASS define a class via bytecode
        """

        from koskari.lib import NamespaceSpec

        collect_nspec = NamespaceSpec("C", (), ())
        code_obj, ns = build_expr([
            ["CONST", "C"],
            ["CONST", ()],
            ["BUILD_ARGS", 2],
            ["PUSH_COLLECT", collect_nspec],
            ["POP_COLLECT", 0],
            ["BUILD_CLASS", 0],
            ["RETURN", 1],
        ])
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertIsInstance(result, type)
        self.assertEqual(result.__name__, "C")
        self.assertIs(type(result), type)


class TestPushCollectErrorPath(CevalTest):

    async def test_push_collect_raises_with_invalid_constant(self):
        """
        PUSH_COLLECT with invalid constant (int, not NamespaceSpec) is
        refused with TypeError when the Code is built
        """

        with self.assertRaises(TypeError) as caught:
            build_expr([
                ["PUSH_COLLECT", 0],
                ["POP_COLLECT", 0],
                ["RETURN", 1],
            ])
        self.assertIn("PUSH_COLLECT", str(caught.exception))


# The end.
