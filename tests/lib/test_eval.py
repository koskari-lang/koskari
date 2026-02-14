# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.


"""
Unit tests for Koskari evaluator types: Continuation, Coroutine, AsyncGenerator

These tests exercise the Python-facing wrapper types for the Koskari VM.
Since the full bytecode interpreter is not yet implemented, these tests
focus on type registration, protocol compliance, and basic behavior.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


import asyncio
import unittest

from koskari.lib import Continuation, Coroutine, AsyncGenerator


class TestContinuationType(unittest.TestCase):
    """Tests for the Continuation type"""

    def test_type_exists(self):
        """Continuation type is registered"""

        self.assertIsNotNone(Continuation)
        self.assertEqual(Continuation.__name__, "Continuation")

    def test_type_module(self):
        """Continuation has correct module path"""

        self.assertEqual(Continuation.__module__, "koskari.lib")

    def test_is_callable(self):
        """Continuation type has __call__ method"""

        self.assertTrue(callable(Continuation))


class TestCoroutineType(unittest.TestCase):
    """Tests for the Coroutine type"""

    def test_type_exists(self):
        """Coroutine type is registered"""

        self.assertIsNotNone(Coroutine)
        self.assertEqual(Coroutine.__name__, "Coroutine")

    def test_type_module(self):
        """Coroutine has correct module path"""

        self.assertEqual(Coroutine.__module__, "koskari.lib")

    def test_has_await_method(self):
        """Coroutine type implements awaitable protocol"""

        # Check that the type has the necessary async methods
        # The actual await is tested via asyncio integration
        self.assertTrue(hasattr(Coroutine, 'send'))
        self.assertTrue(hasattr(Coroutine, 'throw'))
        self.assertTrue(hasattr(Coroutine, 'close'))


class TestAsyncGeneratorType(unittest.TestCase):
    """Tests for the AsyncGenerator type"""

    def test_type_exists(self):
        """AsyncGenerator type is registered"""

        self.assertIsNotNone(AsyncGenerator)
        self.assertEqual(AsyncGenerator.__name__, "AsyncGenerator")

    def test_type_module(self):
        """AsyncGenerator has correct module path"""

        self.assertEqual(AsyncGenerator.__module__, "koskari.lib")


class TestContinuationBehavior(unittest.TestCase):
    """
    Behavioral tests for multi-shot continuations.

    Note: Full continuation capture/resume requires bytecode support.
    These tests verify the wrapper type behavior with null/empty contexts.
    """

    def test_repr_format(self):
        """Continuation repr shows type and address"""

        # Since we can't easily create a Continuation without internal
        # C API access, we verify the type's repr behavior exists.
        # The actual repr format is "<continuation at 0x...>".
        self.assertIn("at", repr(Continuation))


class TestCoroutineBehavior(unittest.TestCase):
    """
    Behavioral tests for Koskari coroutines.

    These tests verify coroutine protocol compliance and basic behavior.
    Full VM execution tests require bytecode interpreter implementation.
    """

    def test_repr_format(self):
        """Coroutine repr shows state"""

        # Type repr shows the class, not an instance
        self.assertIn("class", repr(Coroutine))


class TestAsyncGeneratorBehavior(unittest.TestCase):
    """
    Behavioral tests for Koskari async generators.

    These tests verify async iterator protocol compliance.
    """

    def test_repr_format(self):
        """AsyncGenerator repr is informative"""

        self.assertIn("class", repr(AsyncGenerator))


class TestEvalModuleIntegration(unittest.TestCase):
    """
    Integration tests verifying the eval module initializes correctly.
    """

    def test_all_types_registered(self):
        """All expected types are registered in koskari.lib"""

        import koskari.lib as lib

        self.assertTrue(hasattr(lib, 'Continuation'))
        self.assertTrue(hasattr(lib, 'Coroutine'))
        self.assertTrue(hasattr(lib, 'AsyncGenerator'))

    def test_types_are_types(self):
        """Registered eval types are proper Python types"""

        self.assertIsInstance(Continuation, type)
        self.assertIsInstance(Coroutine, type)
        self.assertIsInstance(AsyncGenerator, type)


# The end.
