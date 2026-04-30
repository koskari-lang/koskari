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
from koskari.lib import build_class, KosType

import pytest

pytestmark = pytest.mark.phase_lib


class TestBuildClassHelper(unittest.IsolatedAsyncioTestCase):

    async def test_default_metaclass_is_kostype(self):
        """
        build_class without bases uses KosType metaclass
        """

        cls = build_class("Simple", (), {"x": 1})
        self.assertIs(type(cls), KosType)
        self.assertEqual(1, cls.x)


    async def test_composite_metaclass_and_prepare(self):
        """
        Non-type first base uses composite metaclass and __prepare__
        """

        class ForeignMeta(type):

            prepare_calls = 0

            @classmethod
            def __prepare__(mcls, name, bases):
                mcls.prepare_calls += 1
                return {"prepared_flag": True}

        class Base(metaclass=ForeignMeta):
            pass

        cls = build_class("Derived", (Base,), {"x": 7})

        self.assertTrue(issubclass(type(cls), ForeignMeta))
        self.assertTrue(issubclass(type(cls), KosType))
        self.assertEqual(1, ForeignMeta.prepare_calls)
        self.assertTrue(cls.prepared_flag)
        self.assertEqual(7, cls.x)


    async def test_composite_metaclass_cache_reuse(self):
        """
        Composite metaclass is reused for repeated incoming metaclass
        """

        class ForeignMeta(type):
            pass

        class Base(metaclass=ForeignMeta):
            pass

        c1 = build_class("One", (Base,), {"x": 1})
        c2 = build_class("Two", (Base,), {"y": 2})

        self.assertIs(type(c1), type(c2))


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


# The end.
