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
Tests for demand_call: invoke Koskari callables synchronously without suspension.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import (
    KoskariSyncViolation,
    demand,
    demand_apply,
    demand_call,
)
from koskari.types import FormalsSpec
from tests import make_callable
from tests.lib.opcode import build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestDemandSyncExpression(unittest.TestCase):
    """
    demand_call on Expression (no args)
    """

    def test_return_value(self):
        """
        demand_call(expr) returns value for CONST RETURN
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        self.assertEqual(demand_call(expr), 42)


    def test_raise_propagation(self):
        """
        demand_call(expr) propagates RAISE
        """
        code, ns = build_expr(
            [
                ["VAR", "exc"],
                ["RAISE", 1],
                ["RETURN", 0],
            ],
            values={"exc": ValueError("demand_call raise test")},
        )
        expr = make_callable(code, ns)
        with self.assertRaises(ValueError) as ctx:
            demand_call(expr)
        self.assertEqual(ctx.exception.args, ("demand_call raise test",))


    def test_yield_violation(self):
        """
        demand_call(expr) raises KoskariSyncViolation on YIELD
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_call(expr)
        self.assertIn("suspension", str(ctx.exception))


class TestDemandSyncFunctionClosure(unittest.IsolatedAsyncioTestCase):
    """
    demand_call on Function with closures (reproducer for closure-slot bug)

    Simpler case: no args, one closure. Slot 0 = closure 'x'.
    (define later [] (+ x 5))-style with x in enclosing scope.
    """

    def _make_closure_fn_no_args(self):
        """Return fn() -> x with x=100, no parameters"""
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('x',),
            values={'x': 100},
        )
        code = build_code(
            [["FAST", 0], ["RETURN", 1]],
            local_syms=(),
            closure_syms=('x',),
            name='closure_fn',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        return fn


    async def test_function_closure_no_args_via_await(self):
        """
        Function closing over x (no args) returns 100 via await
        """
        fn = self._make_closure_fn_no_args()
        result = await fn()
        self.assertEqual(result, 100)


    def test_function_closure_no_args_via_demand_call(self):
        """
        demand_call on Function with closure; fn() -> 100 (reproducer)
        """
        fn = self._make_closure_fn_no_args()
        self.assertEqual(demand_call(fn), 100)


class TestDemandGenerator(unittest.TestCase):
    """
    demand_call on yielding KosFunction returns DemandGenerator
    """

    def test_yielding_function_returns_generator(self):
        """
        demand_call(yielding_fn) returns a generator, not KoskariSyncViolation
        """
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            values={},
        )
        code = build_code(
            [
                ["CONST", 1],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            name='yielding_fn',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        result = demand_call(fn)
        self.assertIsNotNone(result)
        self.assertTrue(hasattr(result, '__next__'))
        self.assertIs(iter(result), result)
        self.assertEqual(next(result), 1)


    def test_yielding_function_list(self):
        """
        list(demand_call(yielding_fn)) yields expected values
        """
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            values={},
        )
        code = build_code(
            [
                ["CONST", 1],
                ["YIELD", 1],
                ["CONST", 2],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            name='yielding_fn',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        result = demand_call(fn)
        self.assertEqual(list(result), [1, 2])


    def test_await_in_demand_generator_raises_violation(self):
        """
        Generator that awaits raises KoskariSyncViolation when the await
        suspends. Uses a custom awaitable that yields on first send.
        """

        def _suspending_iter():
            yield
            return None

        class SuspendingAwaitable:
            def __await__(self):
                return _suspending_iter()

        awaitable = SuspendingAwaitable()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        code = build_code(
            [
                ["FAST", 0],
                ["AWAIT", 0],
                ["CONST", 1],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='await_then_yield',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        gen = demand_call(fn)
        with self.assertRaises(KoskariSyncViolation) as ctx:
            next(gen)
        self.assertIn('suspension', str(ctx.exception))


class TestDemandSyncTryExceptAwaitInject(unittest.TestCase):
    """
    Reproducer: demand_call injects KoskariSyncViolation via a second
    koskari_eval; an enclosing PUSH_TRY handler should catch it and return.

    Currently try_local_exception_handler does not deliver the raise to TRY
    when DYN_TOP is the current frame (e.g. after CALL left the callee frame
    on top), so demand_call surfaces KoskariSyncViolation instead of the
    handler result. This test asserts the desired behavior and fails until
    that is fixed.
    """

    def test_try_except_catches_injected_violation_under_demand_call(self):
        """
        Suspending await under PUSH_TRY; except body should run and return
        a sentinel. demand_call currently raises KoskariSyncViolation.
        """

        def _suspending_iter():
            yield
            return None

        class SuspendingAwaitable:
            def __await__(self):
                return _suspending_iter()

        awaitable = SuspendingAwaitable()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        code = build_code(
            [
                ["PUSH_TRY", "handler"],
                ["VAR", "a"],
                ["AWAIT", 0],
                ["CLEAR_TRY", 0],
                ["JUMP", "end"],
                ["LABEL", "handler"],
                ["EXCEPTION", 0],
                ["POP", 1],
                ["CONST", "caught"],
                ["JUMP", "end"],
                ["LABEL", "end"],
                ["POP_TRY", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='demand_try_await_inject_repro',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        self.assertEqual(demand_call(fn), "caught")

    def test_await_iter_receives_throw_before_vm_except_path(self):
        """
        Resume with throw calls iterator.throw first. If the __await__ iterator
        catches KoskariSyncViolation and returns, await completes with that
        value (no local TRY needed).
        """

        def _it():
            try:
                yield 1
            except KoskariSyncViolation:
                return "from_iter"

        class SwallowsViolation:
            def __await__(self):
                return _it()

        awaitable = SwallowsViolation()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        code = build_code(
            [
                ["VAR", "a"],
                ["AWAIT", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='demand_await_iter_throw_swallow',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        self.assertEqual(demand_call(fn), "from_iter")


class TestDemandSyncPlainPython(unittest.TestCase):
    """
    demand_call on plain Python callables
    """

    def test_plain_python_callable(self):
        """
        demand_call(lambda: 99) returns 99
        """
        self.assertEqual(demand_call(lambda: 99), 99)


    def test_plain_python_raising(self):
        """
        demand_call(lambda: 1/0) propagates ZeroDivisionError
        """
        with self.assertRaises(ZeroDivisionError):
            demand_call(lambda: 1 / 0)


class TestDemandWrapperAndApply(unittest.TestCase):
    """
    Demand adapter and demand_apply agree with demand_call
    """

    def test_demand_wrapper_matches_call(self):
        """
        demand(fn)() matches demand_call(fn)
        """
        fn = lambda: 42
        wrapped = demand(fn)
        self.assertEqual(wrapped(), demand_call(fn))
        self.assertIs(wrapped.__wrapped__, fn)


    def test_demand_apply_matches_call(self):
        """
        demand_apply(fn, args, None) matches demand_call(fn, *args)
        """
        fn = lambda x, y: x + y
        self.assertEqual(
            demand_apply(fn, (2, 3), None),
            demand_call(fn, 2, 3),
        )


# The end.
