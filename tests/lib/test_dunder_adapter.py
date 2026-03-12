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
Tests for dunder adapter: KosType slot wiring via demand_sync for Python sync
protocols (obj + 1, repr(obj), str(obj)).
"""

import unittest

from koskari.lib import KoskariSyncViolation, KosType
from koskari.types import FormalsSpec
from tests import make_callable
from tests.lib.opcode import build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestDunderAdapterAdd(unittest.TestCase):
    """
    __add__ via nb_add slot: obj + other uses demand_sync
    """

    def test_add_plain_python_dunder(self):
        """
        KosType class with Python __add__: obj + 1 returns correct value
        """
        class Addable(metaclass=KosType):

            def __init__(self, x):
                self.x = x

            def __add__(self, other):
                return self.x + other

        obj = Addable(10)
        self.assertEqual(obj + 5, 15)


    def test_repr_plain_python_dunder(self):
        """
        KosType class with Python __repr__: repr(obj) returns correct string
        """
        class Reprable(metaclass=KosType):

            def __init__(self, tag):
                self.tag = tag

            def __repr__(self):
                return 'Reprable(%r)' % self.tag

        obj = Reprable('foo')
        self.assertEqual(repr(obj), "Reprable('foo')")


    def test_str_plain_python_dunder(self):
        """
        KosType class with Python __str__: str(obj) returns correct string
        """
        class Strable(metaclass=KosType):

            def __init__(self, label):
                self.label = label

            def __str__(self):
                return 'Strable<%s>' % self.label

        obj = Strable('x')
        self.assertEqual(str(obj), 'Strable<x>')


class TestDunderAdapterRichCompare(unittest.TestCase):
    """
    __eq__, __lt__ via tp_richcompare: obj == x, obj < x use demand_sync
    """

    def test_eq_plain_python_dunder(self):
        """
        KosType class with Python __eq__: obj == x returns correct value
        """
        class Equatable(metaclass=KosType):

            def __init__(self, val):
                self.val = val

            def __eq__(self, other):
                if isinstance(other, Equatable):
                    return self.val == other.val
                return self.val == other

        obj = Equatable(42)
        self.assertEqual(obj == 42, True)
        self.assertEqual(obj == 0, False)
        self.assertEqual(obj != 42, False)
        self.assertEqual(obj != 0, True)


    def test_lt_plain_python_dunder(self):
        """
        KosType class with Python __lt__: obj < x returns correct value
        """
        class LessThan(metaclass=KosType):

            def __init__(self, val):
                self.val = val

            def __lt__(self, other):
                if isinstance(other, LessThan):
                    return self.val < other.val
                return self.val < other

        obj = LessThan(10)
        self.assertEqual(obj < 20, True)
        self.assertEqual(obj < 5, False)


class TestDunderAdapterLen(unittest.TestCase):
    """
    __len__ via sq_length: len(obj) uses demand_sync
    """

    def test_len_plain_python_dunder(self):
        """
        KosType class with Python __len__: len(obj) returns correct value
        """
        class Lenable(metaclass=KosType):

            def __init__(self, n):
                self.n = n

            def __len__(self):
                return self.n

        obj = Lenable(7)
        self.assertEqual(len(obj), 7)


class TestDunderAdapterBool(unittest.TestCase):
    """
    __bool__ via nb_bool: bool(obj) uses demand_sync
    """

    def test_bool_plain_python_dunder(self):
        """
        KosType class with Python __bool__: bool(obj) and if obj work
        """
        class Boolable(metaclass=KosType):

            def __init__(self, flag):
                self.flag = flag

            def __bool__(self):
                return self.flag

        t = Boolable(True)
        f = Boolable(False)
        self.assertEqual(bool(t), True)
        self.assertEqual(bool(f), False)
        self.assertTrue(t)
        self.assertFalse(f)


class TestDunderAdapterKoskariSyncViolation(unittest.TestCase):
    """
    KoskariSyncViolation raised when Koskari dunder suspends (AWAIT/YIELD)
    """

    def test_add_koskari_yield_raises_violation(self):
        """
        KosType class with Koskari __add__ that yields raises KoskariSyncViolation
        """
        code = build_code(
            [
                ['CONST', 1],
                ['YIELD', 1],
                ['RETURN', 0],
            ],
            local_syms=('self', 'other'),
            name='__add__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('self', 'other'),
        )
        formals = FormalsSpec(('self', 'other'))
        yielding_add = make_callable(code, ns, formals)

        class BadAdd(metaclass=KosType):

            def __init__(self):
                pass

            __add__ = yielding_add

        obj = BadAdd()
        with self.assertRaises(KoskariSyncViolation) as ctx:
            _ = obj + 1
        self.assertIn('suspension', str(ctx.exception))


    def test_eq_koskari_yield_raises_violation(self):
        """
        KosType class with Koskari __eq__ that yields raises KoskariSyncViolation
        """
        code = build_code(
            [
                ['CONST', 0],
                ['YIELD', 1],
                ['RETURN', 1],
            ],
            local_syms=('self', 'other'),
            name='__eq__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('self', 'other'),
        )
        formals = FormalsSpec(('self', 'other'))
        yielding_eq = make_callable(code, ns, formals)

        class BadEq(metaclass=KosType):

            def __init__(self):
                pass

            __eq__ = yielding_eq

        obj = BadEq()
        with self.assertRaises(KoskariSyncViolation) as ctx:
            _ = obj == 1
        self.assertIn('suspension', str(ctx.exception))


    def test_len_koskari_yield_raises_violation(self):
        """
        KosType class with Koskari __len__ that yields raises KoskariSyncViolation
        """
        code = build_code(
            [
                ['CONST', 0],
                ['YIELD', 1],
                ['RETURN', 1],
            ],
            local_syms=('self',),
            name='__len__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('self',),
        )
        formals = FormalsSpec(('self',))
        yielding_len = make_callable(code, ns, formals)

        class BadLen(metaclass=KosType):

            def __init__(self):
                pass

            __len__ = yielding_len

        obj = BadLen()
        with self.assertRaises(KoskariSyncViolation) as ctx:
            _ = len(obj)
        self.assertIn('suspension', str(ctx.exception))


    def test_bool_koskari_yield_raises_violation(self):
        """
        KosType class with Koskari __bool__ that yields raises KoskariSyncViolation
        """
        code = build_code(
            [
                ['CONST', 1],
                ['YIELD', 1],
                ['RETURN', 1],
            ],
            local_syms=('self',),
            name='__bool__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('self',),
        )
        formals = FormalsSpec(('self',))
        yielding_bool = make_callable(code, ns, formals)

        class BadBool(metaclass=KosType):

            def __init__(self):
                pass

            __bool__ = yielding_bool

        obj = BadBool()
        with self.assertRaises(KoskariSyncViolation) as ctx:
            _ = bool(obj)
        self.assertIn('suspension', str(ctx.exception))
