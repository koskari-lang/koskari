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
Unit tests for reducing and special-case operators

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4 via Cursor
"""


import unittest

from koskari import lib


class TestOperatorAdd(unittest.TestCase):
    """Tests for the + reducing operator"""

    def test_unary_positive(self):
        """unary + calls __pos__"""

        add = getattr(lib, '+')
        result = add(5)
        self.assertEqual(result, 5)

    def test_unary_negative_number(self):
        """unary + on negative number"""

        add = getattr(lib, '+')
        result = add(-5)
        self.assertEqual(result, -5)

    def test_binary_add(self):
        """binary + adds two values"""

        add = getattr(lib, '+')
        result = add(3, 4)
        self.assertEqual(result, 7)

    def test_reduce_multiple(self):
        """+ reduces multiple values"""

        add = getattr(lib, '+')
        result = add(1, 2, 3, 4, 5)
        self.assertEqual(result, 15)

    def test_no_args_error(self):
        """+ with no args raises TypeError"""

        add = getattr(lib, '+')
        with self.assertRaises(TypeError):
            add()

    def test_strings(self):
        """+ concatenates strings"""

        add = getattr(lib, '+')
        result = add("hello", " ", "world")
        self.assertEqual(result, "hello world")

    def test_lists(self):
        """+ concatenates lists"""

        add = getattr(lib, '+')
        result = add([1, 2], [3, 4])
        self.assertEqual(result, [1, 2, 3, 4])


class TestOperatorSubtract(unittest.TestCase):
    """Tests for the - reducing operator"""

    def test_unary_negate(self):
        """unary - negates value"""

        sub = getattr(lib, '-')
        result = sub(5)
        self.assertEqual(result, -5)

    def test_unary_double_negate(self):
        """unary - on negative returns positive"""

        sub = getattr(lib, '-')
        result = sub(-5)
        self.assertEqual(result, 5)

    def test_binary_subtract(self):
        """binary - subtracts two values"""

        sub = getattr(lib, '-')
        result = sub(10, 3)
        self.assertEqual(result, 7)

    def test_reduce_multiple(self):
        """- reduces left to right"""

        sub = getattr(lib, '-')
        result = sub(100, 20, 10, 5)
        self.assertEqual(result, 65)

    def test_no_args_error(self):
        """- with no args raises TypeError"""

        sub = getattr(lib, '-')
        with self.assertRaises(TypeError):
            sub()


class TestOperatorMultiply(unittest.TestCase):
    """Tests for the * reducing operator"""

    def test_binary_multiply(self):
        """binary * multiplies two values"""

        mul = getattr(lib, '*')
        result = mul(3, 4)
        self.assertEqual(result, 12)

    def test_reduce_multiple(self):
        """* reduces multiple values"""

        mul = getattr(lib, '*')
        result = mul(2, 3, 4)
        self.assertEqual(result, 24)

    def test_no_args_error(self):
        """* with no args raises TypeError"""

        mul = getattr(lib, '*')
        with self.assertRaises(TypeError):
            mul()

    def test_one_arg_error(self):
        """* with one arg raises TypeError"""

        mul = getattr(lib, '*')
        with self.assertRaises(TypeError):
            mul(5)

    def test_string_repeat(self):
        """* with string and int repeats string"""

        mul = getattr(lib, '*')
        result = mul("ab", 3)
        self.assertEqual(result, "ababab")


class TestOperatorTrueDivide(unittest.TestCase):
    """Tests for the / true division operator"""

    def test_binary_divide(self):
        """binary / divides two values"""

        div = getattr(lib, '/')
        result = div(10, 2)
        self.assertEqual(result, 5.0)

    def test_reduce_multiple(self):
        """/ reduces multiple values"""

        div = getattr(lib, '/')
        result = div(100, 2, 5)
        self.assertEqual(result, 10.0)

    def test_no_args_error(self):
        """/ with no args raises TypeError"""

        div = getattr(lib, '/')
        with self.assertRaises(TypeError):
            div()

    def test_one_arg_error(self):
        """/ with one arg raises TypeError"""

        div = getattr(lib, '/')
        with self.assertRaises(TypeError):
            div(10)


class TestOperatorFloorDivide(unittest.TestCase):
    """Tests for the // floor division operator"""

    def test_binary_floordiv(self):
        """binary // floor divides two values"""

        floordiv = getattr(lib, '//')
        result = floordiv(10, 3)
        self.assertEqual(result, 3)

    def test_reduce_multiple(self):
        """// reduces multiple values"""

        floordiv = getattr(lib, '//')
        result = floordiv(100, 3, 2)
        self.assertEqual(result, 16)

    def test_no_args_error(self):
        """// with no args raises TypeError"""

        floordiv = getattr(lib, '//')
        with self.assertRaises(TypeError):
            floordiv()

    def test_one_arg_error(self):
        """// with one arg raises TypeError"""

        floordiv = getattr(lib, '//')
        with self.assertRaises(TypeError):
            floordiv(10)


class TestOperatorBitwiseAnd(unittest.TestCase):
    """Tests for the & bitwise AND operator"""

    def test_binary_and(self):
        """binary & performs bitwise AND"""

        band = getattr(lib, '&')
        result = band(0b1111, 0b1010)
        self.assertEqual(result, 0b1010)

    def test_reduce_multiple(self):
        """& reduces multiple values"""

        band = getattr(lib, '&')
        result = band(0b1111, 0b1110, 0b1100)
        self.assertEqual(result, 0b1100)

    def test_no_args_error(self):
        """& with no args raises TypeError"""

        band = getattr(lib, '&')
        with self.assertRaises(TypeError):
            band()

    def test_one_arg_error(self):
        """& with one arg raises TypeError"""

        band = getattr(lib, '&')
        with self.assertRaises(TypeError):
            band(0b1111)


class TestOperatorBitwiseOr(unittest.TestCase):
    """Tests for the | bitwise OR operator"""

    def test_binary_or(self):
        """binary | performs bitwise OR"""

        bor = getattr(lib, '|')
        result = bor(0b1100, 0b0011)
        self.assertEqual(result, 0b1111)

    def test_reduce_multiple(self):
        """| reduces multiple values"""

        bor = getattr(lib, '|')
        result = bor(0b1000, 0b0100, 0b0010, 0b0001)
        self.assertEqual(result, 0b1111)

    def test_no_args_error(self):
        """| with no args raises TypeError"""

        bor = getattr(lib, '|')
        with self.assertRaises(TypeError):
            bor()

    def test_one_arg_error(self):
        """| with one arg raises TypeError"""

        bor = getattr(lib, '|')
        with self.assertRaises(TypeError):
            bor(0b1111)


class TestOperatorBitwiseXor(unittest.TestCase):
    """Tests for the ^ bitwise XOR operator"""

    def test_binary_xor(self):
        """binary ^ performs bitwise XOR"""

        bxor = getattr(lib, '^')
        result = bxor(0b1100, 0b1010)
        self.assertEqual(result, 0b0110)

    def test_reduce_multiple(self):
        """^ reduces multiple values"""

        bxor = getattr(lib, '^')
        result = bxor(0b1111, 0b0101, 0b0011)
        self.assertEqual(result, 0b1001)

    def test_no_args_error(self):
        """^ with no args raises TypeError"""

        bxor = getattr(lib, '^')
        with self.assertRaises(TypeError):
            bxor()

    def test_one_arg_error(self):
        """^ with one arg raises TypeError"""

        bxor = getattr(lib, '^')
        with self.assertRaises(TypeError):
            bxor(0b1111)


class TestOperatorMatrixMultiply(unittest.TestCase):
    """Tests for the @ matrix multiply operator"""

    def test_no_args_error(self):
        """@ with no args raises TypeError"""

        matmul = getattr(lib, '@')
        with self.assertRaises(TypeError):
            matmul()

    def test_one_arg_error(self):
        """@ with one arg raises TypeError"""

        matmul = getattr(lib, '@')
        with self.assertRaises(TypeError):
            matmul(5)


class TestOperatorIn(unittest.TestCase):
    """Tests for the in membership operator"""

    def test_in_list_true(self):
        """in returns True when element in list"""

        op_in = getattr(lib, 'in')
        result = op_in(2, [1, 2, 3])
        self.assertTrue(result)

    def test_in_list_false(self):
        """in returns False when element not in list"""

        op_in = getattr(lib, 'in')
        result = op_in(5, [1, 2, 3])
        self.assertFalse(result)

    def test_in_string_true(self):
        """in works with strings"""

        op_in = getattr(lib, 'in')
        result = op_in("ell", "hello")
        self.assertTrue(result)

    def test_in_string_false(self):
        """in returns False when substring not found"""

        op_in = getattr(lib, 'in')
        result = op_in("xyz", "hello")
        self.assertFalse(result)

    def test_in_dict_key(self):
        """in checks dict keys"""

        op_in = getattr(lib, 'in')
        result = op_in('a', {'a': 1, 'b': 2})
        self.assertTrue(result)

    def test_in_wrong_args_error(self):
        """in with wrong number of args raises TypeError"""

        op_in = getattr(lib, 'in')
        with self.assertRaises(TypeError):
            op_in(1)
        with self.assertRaises(TypeError):
            op_in(1, 2, 3)


class TestOperatorNotIn(unittest.TestCase):
    """Tests for the not-in membership operator"""

    def test_not_in_list_false(self):
        """not-in returns False when element in list"""

        op_not_in = getattr(lib, 'not-in')
        result = op_not_in(2, [1, 2, 3])
        self.assertFalse(result)

    def test_not_in_list_true(self):
        """not-in returns True when element not in list"""

        op_not_in = getattr(lib, 'not-in')
        result = op_not_in(5, [1, 2, 3])
        self.assertTrue(result)

    def test_not_in_string_false(self):
        """not-in returns False when substring found"""

        op_not_in = getattr(lib, 'not-in')
        result = op_not_in("ell", "hello")
        self.assertFalse(result)

    def test_not_in_string_true(self):
        """not-in returns True when substring not found"""

        op_not_in = getattr(lib, 'not-in')
        result = op_not_in("xyz", "hello")
        self.assertTrue(result)

    def test_not_in_wrong_args_error(self):
        """not-in with wrong number of args raises TypeError"""

        op_not_in = getattr(lib, 'not-in')
        with self.assertRaises(TypeError):
            op_not_in(1)
        with self.assertRaises(TypeError):
            op_not_in(1, 2, 3)


class TestOperatorBuildStr(unittest.TestCase):
    """Tests for the build-str string concatenation operator"""

    def test_empty(self):
        """build-str with no args returns empty string"""

        build_str = getattr(lib, 'build-str')
        result = build_str()
        self.assertEqual(result, "")

    def test_single_string(self):
        """build-str with single string returns it"""

        build_str = getattr(lib, 'build-str')
        result = build_str("hello")
        self.assertEqual(result, "hello")

    def test_single_non_string(self):
        """build-str converts non-string to string"""

        build_str = getattr(lib, 'build-str')
        result = build_str(42)
        self.assertEqual(result, "42")

    def test_multiple_strings(self):
        """build-str concatenates multiple strings"""

        build_str = getattr(lib, 'build-str')
        result = build_str("hello", " ", "world")
        self.assertEqual(result, "hello world")

    def test_mixed_types(self):
        """build-str concatenates mixed types"""

        build_str = getattr(lib, 'build-str')
        result = build_str("value: ", 42, ", ratio: ", 3.14)
        self.assertEqual(result, "value: 42, ratio: 3.14")

    def test_none_value(self):
        """build-str handles None"""

        build_str = getattr(lib, 'build-str')
        result = build_str("prefix: ", None)
        self.assertEqual(result, "prefix: None")


# The end.
