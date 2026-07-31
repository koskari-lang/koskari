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
tests.lib.test_asyncgen_protocol

The PEP 525 method surface on koskari.lib.AsyncGenerator: asend and
athrow as generator methods (not just send/throw on the __anext__
awaitable), with CPython's semantics for delivery, exhaustion, and the
just-started guard. Parity scenarios that need generator bodies with
try/except or awaits live in tests.reproducers.test_asyncgen_protocol.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import collections.abc

from koskari.lib import AsyncGenerator
from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


def make_agen(steps=2):
    """
    Build an async generator that yields 1, then echoes each sent value
    once per remaining YIELD.
    """

    code = [["CONST", 1]]
    for _ in range(steps):
        code.append(["YIELD", 1])
    code.append(["RETURN", 0])
    code_obj, ns = build_expr(code)
    agen = make_callable(code_obj, ns)()
    assert isinstance(agen, AsyncGenerator)
    return agen


class TestAsend(CevalTest):
    """
    asend on the generator object.
    """

    async def test_asend_none_starts(self):
        """
        asend(None) is the first step, like __anext__
        """

        agen = make_agen()
        self.assertEqual(await agen.asend(None), 1)


    async def test_asend_delivers_value(self):
        """
        the sent value becomes the result of the suspended yield
        """

        agen = make_agen()
        self.assertEqual(await agen.asend(None), 1)
        self.assertEqual(await agen.asend(123), 123)


    async def test_asend_non_none_just_started(self):
        """
        sending a value before the first step is a TypeError, and the
        generator remains usable
        """

        agen = make_agen()
        with self.assertRaises(TypeError):
            await agen.asend(123)
        self.assertEqual(await agen.asend(None), 1)


    async def test_asend_exhausted(self):
        """
        asend on an exhausted generator raises StopAsyncIteration
        """

        agen = make_agen(steps=1)
        self.assertEqual(await agen.asend(None), 1)
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)


class TestAthrow(CevalTest):
    """
    athrow on the generator object.
    """

    async def test_athrow_uncaught_propagates(self):
        """
        an unhandled athrow exception reaches the awaiter and finishes
        the generator
        """

        agen = make_agen()
        self.assertEqual(await agen.asend(None), 1)
        with self.assertRaises(ValueError) as ctx:
            await agen.athrow(ValueError("boom"))
        self.assertIn("boom", str(ctx.exception))
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)


    async def test_athrow_instance_only(self):
        """
        athrow accepts a bare exception instance
        """

        agen = make_agen()
        self.assertEqual(await agen.asend(None), 1)
        with self.assertRaises(KeyError):
            await agen.athrow(KeyError("missing"))


    async def test_athrow_exhausted(self):
        """
        athrow on an exhausted generator completes quietly with None,
        matching CPython
        """

        agen = make_agen(steps=1)
        self.assertEqual(await agen.asend(None), 1)
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)
        self.assertIsNone(await agen.athrow(ValueError("late")))


class TestAclose(CevalTest):
    """
    aclose on the generator object.
    """

    async def test_aclose_suspended(self):
        """
        aclose on a suspended generator completes with None and closes
        it
        """

        agen = make_agen()
        self.assertEqual(await agen.asend(None), 1)
        self.assertIsNone(await agen.aclose())
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)


    async def test_aclose_never_started(self):
        """
        aclose before the first step closes the generator without
        running it
        """

        agen = make_agen()
        self.assertIsNone(await agen.aclose())
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)


    async def test_aclose_idempotent(self):
        """
        a second aclose is a quiet None
        """

        agen = make_agen()
        self.assertEqual(await agen.asend(None), 1)
        self.assertIsNone(await agen.aclose())
        self.assertIsNone(await agen.aclose())


    async def test_aclose_exhausted(self):
        """
        aclose after exhaustion is a quiet None
        """

        agen = make_agen(steps=1)
        self.assertEqual(await agen.asend(None), 1)
        with self.assertRaises(StopAsyncIteration):
            await agen.asend(None)
        self.assertIsNone(await agen.aclose())


class TestProtocolConformance(CevalTest):
    """
    The structural collections.abc hooks accept the generator.
    """

    async def test_abc_async_generator(self):
        """
        AsyncGenerator satisfies collections.abc.AsyncGenerator
        """

        agen = make_agen()
        self.assertIsInstance(agen, collections.abc.AsyncGenerator)


# The end.
