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

Validates that KosType works from Python: C(...) returns an awaitable,
C.new() provides synchronous construction, and async __init__ is supported
(not normally allowed with plain type).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
import unittest
import warnings

from koskari.lib import KosType, run_sync


def _await_result(awaitable):
    """
    Drive an awaitable to completion via run_sync. asyncio.run expects a
    coroutine, so wrap the awaitable in one.
    """
    async def _run():
        return await awaitable
    return run_sync(_run())


class TestKosTypeImport(unittest.TestCase):

    def test_kostype_importable(self):
        """
        KosType is importable from koskari.lib
        """

        import koskari.lib as lib
        self.assertTrue(hasattr(lib, 'KosType'))
        self.assertTrue(callable(lib.KosType))


    def test_kostype_is_metaclass(self):
        """
        KosType subclasses type and can be used as metaclass
        """

        class C(metaclass=KosType):
            pass

        self.assertIsInstance(C, type)
        self.assertIs(type(C), KosType)


class TestKosTypeSyncInit(unittest.TestCase):

    def test_call_returns_awaitable(self):
        """
        C(...) returns an awaitable yielding the instance
        """

        class C(metaclass=KosType):

            def __init__(self, x=0):
                self.x = x

        aw = C(42)
        self.assertTrue(hasattr(aw, '__await__'))
        instance = _await_result(aw)
        self.assertIsInstance(instance, C)
        self.assertEqual(42, instance.x)


    def test_new_returns_instance_sync(self):
        """
        C.new(...) synchronously constructs and returns the instance
        """

        class C(metaclass=KosType):

            def __init__(self, tag):
                self.tag = tag

        instance = C.new('sync')
        self.assertIsInstance(instance, C)
        self.assertEqual('sync', instance.tag)


    def test_new_no_args(self):
        """
        C.new() works with no-arg __init__
        """

        class C(metaclass=KosType):

            def __init__(self):
                self.ready = True

        instance = C.new()
        self.assertIsInstance(instance, C)
        self.assertTrue(instance.ready)


class TestKosTypeAsyncInit(unittest.TestCase):

    def test_async_init_allowed(self):
        """
        KosType allows async def __init__, which plain type does not
        """

        class AsyncC(metaclass=KosType):

            async def __init__(self, value):
                await asyncio.sleep(0)
                self.value = value

        aw = AsyncC(99)
        self.assertTrue(hasattr(aw, '__await__'))
        instance = _await_result(aw)
        self.assertIsInstance(instance, AsyncC)
        self.assertEqual(99, instance.value)


    def test_async_init_via_new(self):
        """
        C.new(...) runs async __init__ to completion and returns instance
        """

        class AsyncC(metaclass=KosType):

            async def __init__(self, x, y):
                await asyncio.sleep(0)
                self.sum = x + y

        instance = AsyncC.new(10, 32)
        self.assertIsInstance(instance, AsyncC)
        self.assertEqual(42, instance.sum)


    def test_plain_type_rejects_async_init(self):
        """
        Plain type raises when __init__ returns a coroutine
        """

        class Bad(metaclass=type):

            async def __init__(self):
                await asyncio.sleep(0)

        with warnings.catch_warnings():
            # the default type does *not* like an async __init__
            warnings.simplefilter('ignore', RuntimeWarning)

            with self.assertRaises(TypeError) as ctx:
                # which is fine, we're expecting it to break
                Bad()

        self.assertIn('None', str(ctx.exception))


class TestKosTypeNoInit(unittest.TestCase):

    def test_no_init_call_returns_awaitable(self):
        """
        Class without __init__: C() returns awaitable yielding instance
        """

        class C(metaclass=KosType):
            pass

        aw = C()
        instance = _await_result(aw)
        self.assertIsInstance(instance, C)


    def test_no_init_new_returns_instance(self):
        """
        Class without __init__: C.new() returns instance directly
        """

        class C(metaclass=KosType):
            pass

        instance = C.new()
        self.assertIsInstance(instance, C)


# The end.
