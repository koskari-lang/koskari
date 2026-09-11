# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Code() finalization converts and interns each CONST and VAR operand
exactly once: the pool is sized by that single pass and emit reuses
its indices, so an operand whose conversion is not identity-stable
cannot grow the pool after it was allocated

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import Code, NamespaceSpec, OpCode


pytestmark = pytest.mark.phase_lib


class Named(str):
    """
    A str subclass: a str instance PyObject_Str accepts as a name, but
    not an exact str, so the pool never interns it
    """

    pass


class FreshName(object):
    """
    A VAR operand whose str conversion is a new object every call
    """

    def __init__(self):
        self.calls = 0

    def __str__(self):
        self.calls += 1
        return Named("x")


def make(instrs):
    return Code(NamespaceSpec("t", (), ()), instrs, "t")


class TestFinalizePoolOnce(unittest.TestCase):

    def test_var_operand_is_converted_and_interned_once(self):
        name = FreshName()
        code = make([[OpCode.VAR, name], [OpCode.RETURN, 1]])

        self.assertEqual(name.calls, 1)
        self.assertEqual(code.constants_pool, ("x",))
        self.assertEqual(
            [row for row in code.soften() if row[0] == OpCode.VAR],
            [(OpCode.VAR, "x")])

    def test_repeated_var_operand_shares_one_pool_entry(self):
        name = FreshName()
        code = make([
            [OpCode.VAR, name], [OpCode.POP, 1],
            [OpCode.VAR, name], [OpCode.RETURN, 1]])

        self.assertEqual(code.constants_pool, ("x", "x"))
        self.assertEqual(
            [row for row in code.soften() if row[0] == OpCode.VAR],
            [(OpCode.VAR, "x"), (OpCode.VAR, "x")])


# The end.
