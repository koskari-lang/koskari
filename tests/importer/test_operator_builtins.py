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
Tests for the operator functions bound by the Python bootstrap: the
reducing operators the compiled forms accept variadically are the same
reducing functions when called through their builtin name

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest


pytestmark = pytest.mark.phase_basics


class Mat:
    """
    A matmul-only operand that records the fold order
    """

    def __init__(self, tag):
        self.tag = tag

    def __matmul__(self, other):
        return Mat(f"({self.tag}@{other.tag})")


class TestReducingOperatorBuiltins(unittest.TestCase):

    def test_matrix_multiply_builtin_reduces(self):
        import koskari.builtins as builtins

        matmul = vars(builtins)['@']
        a, b, c = Mat("a"), Mat("b"), Mat("c")

        self.assertEqual("(a@b)", (matmul(a, b)).tag)
        self.assertEqual("((a@b)@c)", (matmul(a, b, c)).tag)

    def test_matrix_multiply_builtin_is_the_lib_export(self):
        import koskari.builtins as builtins
        import koskari.lib as lib

        self.assertIs(getattr(lib, '@'), vars(builtins)['@'])


class TestPowerBuiltin(unittest.TestCase):

    def test_power_builtin_takes_modulus(self):
        import koskari.builtins as builtins

        power = vars(builtins)['**']

        self.assertEqual(1024, power(2, 10))
        self.assertEqual(256, power(2, 8, 1000))
        self.assertEqual(56, power(2, 8, 100))

    def test_power_builtin_is_the_lib_export(self):
        import koskari.builtins as builtins
        import koskari.lib as lib

        self.assertIs(getattr(lib, '**'), vars(builtins)['**'])


# The end.
