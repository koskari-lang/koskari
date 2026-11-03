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

    def test_count_at_the_segment_limit_builds(self):
        code = make([[OpCode.CONST, 1], [OpCode.POP, 1024]])
        self.assertIsInstance(code, Code)

    def test_count_over_the_segment_limit_rejected(self):
        """
        An operand segment holds at most 1024 slots, so a count no
        segment could satisfy is refused at construction
        """

        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CONST, 1], [OpCode.POP, 1025]])
        self.assertIn("1024", str(caught.exception))
        with self.assertRaises(ValueError):
            make([[OpCode.CONST, 1], [OpCode.CALL, 1025]])

    def test_pair_count_over_half_the_segment_limit_rejected(self):
        code = make([[OpCode.CONST, 1], [OpCode.BUILD_KWDS, 512]])
        self.assertIsInstance(code, Code)
        with self.assertRaises(ValueError):
            make([[OpCode.CONST, 1], [OpCode.BUILD_KWDS, 513]])

    def test_extra_count_over_the_segment_limit_rejected(self):
        """
        An EXTRA COUNT field past the segment limit is refused at
        construction
        """

        code = make([[OpCode.CALL_VAR, ("f", 1024, None, None)]])
        self.assertIsInstance(code, Code)
        with self.assertRaises(ValueError) as caught:
            make([[OpCode.CALL_VAR, ("f", 1025, None, None)]])
        self.assertIn("1024", str(caught.exception))


class TestLabelResolution(unittest.TestCase):

    def test_int_label_name_equal_to_a_jump_pc_resolves(self):
        """
        A label is looked up by its name alone, so a label named with the
        int that is also some jump's output pc still resolves to the pc
        the label was defined at
        """

        code = make([
            [OpCode.CONST, "a"],
            [OpCode.JUMP, "L"],
            [OpCode.LABEL, "L"],
            [OpCode.JUMP, 1],
            [OpCode.CONST, "x"],
            [OpCode.RETURN, 1],
            [OpCode.LABEL, 1],
            [OpCode.CONST, "b"],
            [OpCode.RETURN, 1],
        ])
        self.assertIsInstance(code, Code)
        self.assertEqual(code.oparg(2)[1], 5)



class TestTailCallExtentValidation(unittest.TestCase):
    """
    A tail call may run through the extents frame handoff can discard,
    NS, LOOP and FOREACH, and through nothing else; the compiler never
    emits the other shape, so a hand-assembled stream carrying it is
    refused when the Code is built
    """

    def test_tail_call_inside_try_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            make([
                [OpCode.PUSH_TRY, "handler"],
                [OpCode.CONST, None],
                [OpCode.TAIL_CALL, 1],
                [OpCode.POP_TRY, 0],
                [OpCode.LABEL, "handler"],
                [OpCode.RETURN, 1],
            ])
        self.assertIn("non-discardable", str(caught.exception))
        self.assertIn("TRY", str(caught.exception))

    def test_tail_call_inside_loop_builds(self):
        code = make([
            [OpCode.PUSH_LOOP, 0],
            [OpCode.CONST, None],
            [OpCode.TAIL_CALL, 1],
            [OpCode.POP_LOOP, 0],
            [OpCode.RETURN, 1],
        ])
        self.assertIsInstance(code, Code)


class TestExtentStructureValidation(unittest.TestCase):
    """
    Every PUSH_* must be closed by its POP_* before the stream ends
    """

    def test_unclosed_extent_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            make([
                [OpCode.PUSH_LOOP, 0],
                [OpCode.CONST, None],
                [OpCode.RETURN, 1],
            ])
        self.assertIn("unclosed extent", str(caught.exception))


# The end.
