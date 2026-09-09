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
Unit tests for koskari.lib.build_class helper.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest
from enum import IntEnum

import pytest

from koskari.lib import FormalsSpec, build_class
from tests import make_callable
from tests.lib.opcode import build_code, build_expr


pytestmark = pytest.mark.phase_lib


class TestBuildClassHelper(unittest.IsolatedAsyncioTestCase):

    async def test_default_metaclass_is_type(self):
        """
        build_class without bases uses plain type as the metaclass
        """

        cls = build_class("Simple", (), {"x": 1})
        self.assertIs(type(cls), type)
        self.assertEqual(1, cls.x)


    async def test_foreign_metaclass_and_prepare(self):
        """
        A base with a foreign metaclass wins directly and __prepare__
        is honored
        """

        prepared_names = []

        class ForeignMeta(type):

            @classmethod
            def __prepare__(mcls, name, bases):
                prepared_names.append(name)
                return {"prepared_flag": True}

        class Base(metaclass=ForeignMeta):
            pass

        cls = build_class("Derived", (Base,), {"x": 7})

        self.assertIs(type(cls), ForeignMeta)
        self.assertEqual(["Base", "Derived"], prepared_names)
        self.assertTrue(cls.prepared_flag)
        self.assertEqual(7, cls.x)


    async def test_foreign_metaclass_shared(self):
        """
        Repeated builds over the same base share the base's metaclass
        """

        class ForeignMeta(type):
            pass

        class Base(metaclass=ForeignMeta):
            pass

        c1 = build_class("One", (Base,), {"x": 1})
        c2 = build_class("Two", (Base,), {"y": 2})

        self.assertIs(type(c1), type(c2))
        self.assertIs(type(c1), ForeignMeta)


    async def test_build_class_with_intenum_base(self):
        """
        build_class supports IntEnum base without explicit metaclass
        """

        cls = build_class(
            'Color',
            (IntEnum,),
            {
                '__module__': __name__,
                'RED': 1,
                'GREEN': 2,
            })

        self.assertTrue(issubclass(cls, IntEnum))
        self.assertEqual(1, cls.RED.value)
        self.assertEqual(2, cls.GREEN.value)


class TestBuildClassDemandWrap(unittest.IsolatedAsyncioTestCase):
    """
    build_class demand-wraps Koskari callables under the slot-backed
    dunder names, so Python protocols dispatch synchronously
    """

    async def test_koskari_init_result_discarded(self):
        """
        Koskari __init__ returning a value: C() still constructs; the
        result is discarded rather than tripping the None-return check
        """

        code = build_code(
            [
                ['CONST', 5],
                ['RETURN', 1],
            ],
            local_syms=('self',),
            name='__init__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('self',),
        )
        init = make_callable(code, ns, FormalsSpec(('self',)))

        cls = build_class("WithInit", (), {'__init__': init})
        obj = cls()
        self.assertIsInstance(obj, cls)


    async def test_koskari_new_demand_wrapped(self):
        """
        Koskari __new__: canonicalized to staticmethod(Demand), so C()
        runs it synchronously with cls as the first positional. The
        sentinel return (not an instance of C) comes back directly,
        proving no coroutine escaped tp_new
        """

        code = build_code(
            [
                ['CONST', 7],
                ['RETURN', 1],
            ],
            local_syms=('cls',),
            name='__new__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('cls',),
        )
        new = make_callable(code, ns, FormalsSpec(('cls',)))

        cls = build_class("WithNew", (), {'__new__': new})
        self.assertEqual(7, cls())


    async def test_koskari_staticmethod_new_demand_wrapped(self):
        """
        Author-written staticmethod over a Koskari __new__ converges on
        the same canonical form and behaves identically
        """

        code = build_code(
            [
                ['CONST', 9],
                ['RETURN', 1],
            ],
            local_syms=('cls',),
            name='__new__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('cls',),
        )
        new = make_callable(code, ns, FormalsSpec(('cls',)))

        cls = build_class("WithStaticNew", (), {'__new__': staticmethod(new)})
        self.assertEqual(9, cls())


    async def test_koskari_class_getitem_classmethod_wrapped(self):
        """
        Koskari __class_getitem__: canonicalized to classmethod(Demand),
        so Cls[x] runs it synchronously with cls bound
        """

        code = build_code(
            [
                ['CONST', 3],
                ['RETURN', 1],
            ],
            local_syms=('cls', 'item'),
            name='__class_getitem__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('cls', 'item'),
        )
        cgi = make_callable(code, ns, FormalsSpec(('cls', 'item')))

        cls = build_class("Subscriptable", (), {'__class_getitem__': cgi})
        self.assertEqual(3, cls[99])


    async def test_koskari_len_demand_wrapped(self):
        """
        Koskari __len__: len(obj) returns the value synchronously
        """

        code = build_code(
            [
                ['CONST', 7],
                ['RETURN', 1],
            ],
            local_syms=('self',),
            name='__len__',
        )
        _, ns = build_expr(
            [['CONST', 0], ['RETURN', 1]],
            local_syms=('self',),
        )
        length = make_callable(code, ns, FormalsSpec(('self',)))

        cls = build_class("Sized", (), {'__len__': length})
        obj = cls()
        self.assertEqual(7, len(obj))


# The end.
