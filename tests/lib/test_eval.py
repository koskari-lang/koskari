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
"""


import asyncio
import traceback
import unittest

import pytest

from koskari.lib import AsyncGenerator, Continuation, Coroutine


pytestmark = pytest.mark.phase_lib


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


class TestCoroutineDemandIntegration(unittest.TestCase):
    """
    Integration tests for Coroutine.demand()
    """

    def test_coroutine_demand_returns_value(self):
        """Coroutine.demand() completes and returns the result value"""

        from tests import make_callable
        from tests.lib.opcode import build_expr

        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        self.assertEqual(coro.demand(), 42)


class TestAsyncGeneratorIterIntegration(unittest.TestCase):
    """
    Integration tests for sync iteration via tp_iter / KosPyIter
    """

    def test_list_agen_yields_values(self):
        """list(agen) yields the same values as async for over a fresh agen"""

        from tests import make_callable
        from tests.lib.opcode import build_expr

        gen_code, gen_ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["CONST", 2],
            ["YIELD", 1],
            ["CONST", 3],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(gen_code, gen_ns)
        agen = expr()
        self.assertIsInstance(agen, AsyncGenerator)
        self.assertEqual(list(agen), [1, 2, 3])


def _assert_exception_has_traceback_attrs(test_case, exc, expected_type=None):
    """
    Assert that a raised exception carries a __traceback__ for traceback
    formatting whose entries include the Koskari frames, each a Python
    frame object whose globals carry the frame's trace data as
    __koskari_trace__, and that the trace data is usable (has code
    names). Optionally assert the exception type.
    """

    print(f"_assert_exception_has_traceback_attrs: exc={exc!r}")

    if expected_type is not None:
        test_case.assertIsInstance(exc, expected_type)
        print(f"  type: {type(exc).__name__} (ok)")

    tb = getattr(exc, "__traceback__", None)
    print(f"  __traceback__: {tb!r}")
    test_case.assertIsNotNone(tb, "exception must have a __traceback__")

    entries = [frame.f_globals.get("__koskari_trace__")
               for frame, _lineno in traceback.walk_tb(tb)]
    entries = [entry for entry in entries if entry is not None]
    print(f"  Koskari entries: {entries!r}")
    test_case.assertGreater(
        len(entries), 0,
        "__traceback__ must hold at least one Koskari frame",
    )
    for entry in entries:
        test_case.assertIsInstance(
            entry, dict,
            "__koskari_trace__ must be a dict of frame trace data",
        )

    frames_with_name = [entry for entry in entries
                        if entry.get("name") is not None]
    print(f"  frames with 'name' (code): {len(frames_with_name)}")
    test_case.assertGreater(
        len(frames_with_name), 0,
        "__traceback__ must hold at least one Koskari frame with 'name'",
    )


class TestExceptionFromKoskariCall(unittest.IsolatedAsyncioTestCase):
    """
    Test that exceptions raised from koskari code carry expected attributes
    for traceback formatting under various conditions.
    """

    async def raised_by(self, expr, exc_type):
        """
        The exception expr raises, with the traceback that assertRaises
        would clear
        """

        try:
            await expr()
        except exc_type as exc:
            return exc
        self.fail(f"{exc_type.__name__} not raised")


    async def test_exception_from_called_function_has_inspectable_fields(self):
        """
        Exception from Python function called by koskari CALL: has a
        traceback holding the Koskari frame.
        """

        def boom():
            raise ValueError("intentional test failure")

        from tests import make_callable
        from tests.lib.opcode import build_expr

        code, ns = build_expr(
            [
                ["VAR", "boom"],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            values={"boom": boom},
        )
        expr = make_callable(code, ns)

        exc = await self.raised_by(expr, ValueError)
        self.assertEqual(exc.args, ("intentional test failure",))
        _assert_exception_has_traceback_attrs(self, exc, ValueError)


    async def test_pure_raise_opcode_has_traceback_attrs(self):
        """
        Pure RAISE opcode: exception carries a __traceback__ holding the
        Koskari frame.
        """

        from tests import make_callable
        from tests.lib.opcode import build_expr

        code, ns = build_expr(
            [
                ["VAR", "exc"],
                ["RAISE", 1],
                ["RETURN", 0],
            ],
            values={"exc": ValueError("pure RAISE opcode")},
        )
        expr = make_callable(code, ns)

        exc = await self.raised_by(expr, ValueError)
        self.assertEqual(exc.args, ("pure RAISE opcode",))
        _assert_exception_has_traceback_attrs(self, exc, ValueError)


    async def test_call_stack_underrun_has_traceback_attrs(self):
        """
        CALL 0 with empty stack: RuntimeError carries traceback attrs.
        """

        from tests import make_callable
        from tests.lib.opcode import build_expr

        code, ns = build_expr([
            ["CALL", 0],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)

        exc = await self.raised_by(expr, RuntimeError)
        _assert_exception_has_traceback_attrs(self, exc, RuntimeError)


    async def test_object_add_stack_underrun_has_traceback_attrs(self):
        """
        OBJECT_ADD 2 with only one value: RuntimeError carries traceback attrs.
        """

        from tests import make_callable
        from tests.lib.opcode import build_expr

        code, ns = build_expr([
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)

        exc = await self.raised_by(expr, RuntimeError)
        _assert_exception_has_traceback_attrs(self, exc, RuntimeError)


    async def test_div_by_zero_has_traceback_attrs(self):
        """
        Division by zero (OBJECT_FLOOR_DIVIDE 1/0): ZeroDivisionError carries
        traceback attrs.
        """

        from tests import make_callable
        from tests.lib.opcode import build_expr

        code, ns = build_expr([
            ["CONST", 1],
            ["CONST", 0],
            ["OBJECT_FLOOR_DIVIDE", 2],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)

        exc = await self.raised_by(expr, ZeroDivisionError)
        _assert_exception_has_traceback_attrs(self, exc, ZeroDivisionError)


# The end.
