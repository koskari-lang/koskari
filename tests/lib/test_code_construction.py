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
Code() validates the shape of its instruction sequence before reading
it, so malformed input raises instead of reading memory it should not

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import Code, NamespaceSpec, OpCode


pytestmark = pytest.mark.phase_lib


def make(instrs):
    return Code(NamespaceSpec("t", (), ()), instrs, "t")


class TestCodeInstructionValidation(unittest.TestCase):

    def test_well_formed_lists_and_tuples_build(self):
        code = make([[OpCode.CONST, 1], (OpCode.RETURN, 1)])
        self.assertIsInstance(code, Code)

    def test_non_sequence_rejected(self):
        with self.assertRaises(TypeError):
            make(7)

    def test_item_not_a_list_or_tuple_rejected(self):
        with self.assertRaises(TypeError):
            make([1])
        # a str is a sequence, but not one the reader may index as a pair
        with self.assertRaises(TypeError):
            make(["ab"])
        with self.assertRaises(TypeError):
            make([range(2)])

    def test_short_item_rejected(self):
        with self.assertRaises(ValueError):
            make([[]])
        with self.assertRaises(ValueError):
            make([(OpCode.CONST,)])

    def test_non_int_opcode_rejected(self):
        with self.assertRaises(TypeError):
            make([("CONST", 1)])
        with self.assertRaises(TypeError):
            make([(None, 1)])

    def test_out_of_range_opcode_rejected(self):
        with self.assertRaises(ValueError):
            make([(999999, 0)])
        with self.assertRaises(ValueError):
            make([(-1, 0)])
        with self.assertRaises(ValueError):
            make([(2 ** 80, 0)])

    def test_error_names_the_offending_instruction(self):
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CONST, 1], [OpCode.RETURN, 1], (999999, 0)])
        self.assertIn("instruction 2", str(caught.exception))


class TestCountArgumentValidation(unittest.TestCase):
    """
    A count-style argument is an int in the 16-bit range; anything else
    is refused instead of truncated or ignored
    """

    def test_count_over_16_bits_rejected(self):
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CONST, 1], [OpCode.RETURN, 65536]])
        self.assertIn("instruction 1", str(caught.exception))

    def test_negative_count_rejected(self):
        with self.assertRaises(ValueError):
            make([[OpCode.CONST, 1], [OpCode.RETURN, -1]])

    def test_non_int_count_rejected(self):
        with self.assertRaises(TypeError):
            make([[OpCode.CONST, 1], [OpCode.RETURN, "1"]])
        with self.assertRaises(TypeError):
            make([[OpCode.CONST, 1], [OpCode.RETURN, None]])

    def test_count_at_16_bit_limit_builds(self):
        code = make([[OpCode.CONST, 1], [OpCode.POP, 65535]])
        self.assertIsInstance(code, Code)


# The end.
