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
Tests for JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr, run_to_completion


class TestJumpOpcodes(CevalTest):
    """
    Tests for JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
    """

    async def test_jump_unconditional(self):
        """
        JUMP branches to label; skips instructions in between
        """
        code, ns = build_expr([
            ["JUMP", "target"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "target"],
            ["CONST", 99],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    async def test_jump_false_jumps_when_falsy(self):
        """
        JUMP_FALSE jumps to label when TOS is false
        """
        code, ns = build_expr([
            ["CONST", False],
            ["JUMP_FALSE", "yes"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "yes"],
            ["CONST", 1],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 1)


    async def test_jump_false_continues_when_truthy(self):
        """
        JUMP_FALSE does not jump when TOS is true
        """
        code, ns = build_expr([
            ["CONST", True],
            ["JUMP_FALSE", "skip"],
            ["CONST", 42],
            ["RETURN", 1],
            ["LABEL", "skip"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


    async def test_jump_true_jumps_when_truthy(self):
        """
        JUMP_TRUE jumps to label when TOS is true
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["JUMP_TRUE", "yes"],
            ["CONST", 0],
            ["RETURN", 1],
            ["LABEL", "yes"],
            ["CONST", 99],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 99)


    async def test_jump_true_continues_when_falsy(self):
        """
        JUMP_TRUE does not jump when TOS is false
        """
        code, ns = build_expr([
            ["CONST", None],
            ["JUMP_TRUE", "skip"],
            ["CONST", 7],
            ["RETURN", 1],
            ["LABEL", "skip"],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 7)


    async def test_jump_and_short_circuit(self):
        """
        JUMP_AND leaves value and jumps when falsy; pops when truthy
        """
        code, ns = build_expr([
            ["CONST", 0],
            ["JUMP_AND", "end"],
            ["CONST", 100],
            ["JUMP_AND", "end"],
            ["CONST", 5],
            ["LABEL", "end"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 0)


    async def test_jump_or_short_circuit(self):
        """
        JUMP_OR leaves value and jumps when truthy; pops when falsy
        """
        code, ns = build_expr([
            ["CONST", 0],
            ["JUMP_OR", "next"],
            ["CONST", 0],
            ["JUMP_OR", "next"],
            ["CONST", 42],
            ["LABEL", "next"],
            ["RETURN", 1],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


# The end.
