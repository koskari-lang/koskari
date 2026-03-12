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
Unit tests for KosType metaclass from koskari.lib.

Validates that KosType works from Python: C(...) returns the instance
directly. Async __init__ is not supported; use a classmethod for async
setup.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

from koskari.lib import KosType

import pytest

pytestmark = pytest.mark.phase_lib


class TestKosTypeImport(unittest.IsolatedAsyncioTestCase):

    async def test_kostype_importable(self):
        """
        KosType is importable from koskari.lib
        """

        import koskari.lib as lib
        self.assertTrue(hasattr(lib, 'KosType'))
        self.assertTrue(callable(lib.KosType))


    async def test_kostype_is_metaclass(self):
        """
        KosType subclasses type and can be used as metaclass
        """

        class C(metaclass=KosType):
            pass

        self.assertIsInstance(C, type)
        self.assertIs(type(C), KosType)


    async def test_kostype_mro_includes_type(self):
        """
        KosType MRO includes type
        """

        self.assertIn(type, KosType.__mro__)


class TestKosTypeSyncInit(unittest.IsolatedAsyncioTestCase):

    async def test_call_returns_instance(self):
        """
        C(...) returns the instance directly
        """

        class C(metaclass=KosType):

            def __init__(self, x=0):
                self.x = x

        instance = C(42)
        self.assertIsInstance(instance, C)
        self.assertEqual(42, instance.x)


class TestKosTypeNoInit(unittest.IsolatedAsyncioTestCase):

    async def test_no_init_call_returns_instance(self):
        """
        Class without __init__: C() returns instance directly
        """

        class C(metaclass=KosType):
            pass

        instance = C()
        self.assertIsInstance(instance, C)


# The end.
