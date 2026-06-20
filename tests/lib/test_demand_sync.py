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
    BoundMethod,
    KoskariSyncViolation,
    demand,
    demand_apply,
    demand_call,
)
from koskari.lib import FormalsSpec
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


    def test_repr_includes_code_name(self):
        """
        DemandGenerator repr matches Python-style <generator object name at ...>
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
        r = repr(result)
        self.assertIn('generator object yielding_fn', r)
        self.assertIn(' at 0x', r)


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


    def test_await0_in_demand_generator_raises_violation(self):
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
                ["AWAIT", 0],
                ["CONST", 1],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            name='await0_then_yield',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        gen = demand_call(fn)
        with self.assertRaises(KoskariSyncViolation) as ctx:
            next(gen)
        self.assertIn('suspension', str(ctx.exception))


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
                ["AWAIT", 1],
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
    In-eval denial: KoskariSyncViolation raised at the AWAIT site so
    TRY/finally can run inside demand-called code.
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
                ["AWAIT", 1],
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

    def test_await_denial_does_not_throw_into_iterator(self):
        """
        In-eval denial (phase 1) raises KoskariSyncViolation before suspending.
        The __await__ iterator is never given a .throw() call; the violation
        propagates as a first-class in-eval exception instead.

        Previously (inject-loop): a second koskari_eval would .throw()
        KoskariSyncViolation into the suspended iterator so it could swallow it
        and return a value. Phase 1 removes that path; iterators that expect
        to catch demand-violation throws must use Koskari TRY instead.
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
                ["AWAIT", 1],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='demand_await_iter_throw_swallow',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_call(fn)
        self.assertIn('suspension', str(ctx.exception))


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


class TestDemandInEvalDenial(unittest.TestCase):
    """
    Phase-1 in-eval denial: KOS_FRAME_DEMAND on the root frame causes
    AWAIT/YIELD to raise KoskariSyncViolation at the deny site so normal
    Koskari exception machinery (TRY/finally, nested frames) can observe it.
    """

    def _suspending_awaitable(self):
        """Return an awaitable that yields on first send (cannot complete eagerly)."""

        def _it():
            yield

        class SuspendingAwaitable:
            def __await__(self):
                return _it()

        return SuspendingAwaitable()


    def test_finally_runs_on_await_denial(self):
        """
        finally block runs when AWAIT is denied on the demand path.
        KOS_FRAME_DEMAND converts the suspension to an in-eval raise so
        PUSH_FINALLY/EXCEPTION/POP_FINALLY can run before propagating.
        """
        sentinel = []

        def mark():
            sentinel.append(True)
            return None

        awaitable = self._suspending_awaitable()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a', 'mark'),
            values={'a': awaitable, 'mark': mark},
        )
        code = build_code(
            [
                # Protected region: AWAIT raises KoskariSyncViolation in demand.
                ["PUSH_FINALLY", "finally_body"],
                ["VAR", "a"],
                ["AWAIT", 1],
                # On exception: jump to finally_body.
                # Normal path also falls through to finally_body here.
                ["LABEL", "finally_body"],
                ["VAR", "mark"],
                ["CALL", 0],
                ["POP", 1],
                ["EXCEPTION", 0],
                ["POP_FINALLY", 0],   # re-raises if exception; pops extent either way
                ["RAISE", 1],         # re-raise: on exception path EXCEPTION pushed exc obj
            ],
            local_syms=(),
            closure_syms=('a', 'mark'),
            name='demand_finally_denial',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        with self.assertRaises(KoskariSyncViolation):
            demand_call(fn)
        self.assertEqual(sentinel, [True])


    def test_nested_call_still_denies(self):
        """
        A callee frame (no KOS_FRAME_DEMAND) still denies await because
        kos_eval_in_demand walks ancestors and finds the marked demand root.
        """
        awaitable = self._suspending_awaitable()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        # Inner function: takes no args, awaits the suspending awaitable.
        inner_code = build_code(
            [
                ["FAST", 0],
                ["AWAIT", 1],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='inner_awaiter',
        )
        inner_fn = make_callable(inner_code, base_ns, FormalsSpec())
        # Outer function: calls inner (inner gets a child frame without DEMAND bit).
        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('inner_fn',),
            values={'inner_fn': inner_fn},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('inner_fn',),
            name='outer_caller',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_call(outer_fn)
        self.assertIn('suspension', str(ctx.exception))


    def test_tail_call_preserves_demand_marker(self):
        """
        TAIL_CALL reuses the current frame via kos_dynframe_reset_for_tail.
        The demand bit must survive that reset so the callee (running on the
        same frame object) still denies await.
        """
        awaitable = self._suspending_awaitable()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        # Inner function: accesses awaitable, awaits it.
        inner_code = build_code(
            [
                ["FAST", 0],
                ["AWAIT", 1],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='inner_awaiter',
        )
        inner_fn = make_callable(inner_code, base_ns, FormalsSpec())
        # Outer function: tail-calls inner (reuses the demand-root frame).
        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('inner_fn',),
            values={'inner_fn': inner_fn},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["TAIL_CALL", 0],
            ],
            local_syms=(),
            closure_syms=('inner_fn',),
            name='outer_tail_caller',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_call(outer_fn)
        self.assertIn('suspension', str(ctx.exception))


class TestDemandDescriptor(unittest.TestCase):
    """
    Demand tp_descr_get binds wrapped descriptors for instance access
    """

    def _self_add_one(self):
        """
        Build KosFunction (self) -> self + 1
        """

        code, base_ns = build_expr([
            ["VAR", "self"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        fspec = FormalsSpec(("self",), None, ())
        return code.function(fspec(), base_ns)


    def test_demand_repr_named_function(self):
        """
        repr(demand(fn)) shows the wrapped callable name
        """

        fn = self._self_add_one()
        wrapped = demand(fn)
        self.assertEqual(repr(wrapped), "<Demand 'expr'>")


    def test_demand_descr_get_class_returns_self(self):
        """
        Demand.__get__(None, owner) returns the unbound Demand wrapper
        """

        fn = self._self_add_one()
        wrapped = demand(fn)
        result = wrapped.__get__(None, type(None))
        self.assertIs(result, wrapped)


    def test_demand_descr_get_instance_wraps_bound_method(self):
        """
        Demand.__get__(obj, owner) returns BoundMethod(Demand(...), obj)
        """

        fn = self._self_add_one()
        wrapped = demand(fn)
        obj = 41
        result = wrapped.__get__(obj, type(obj))
        self.assertIsNot(result, wrapped)
        self.assertIsInstance(result, BoundMethod)
        self.assertIs(result.__func__, wrapped)
        self.assertIs(result.__func__.__wrapped__, fn)
        self.assertIs(result.__self__, obj)


    def test_boundmethod_demand_call_sync(self):
        """
        BoundMethod(Demand(fn), obj)() runs synchronously on the demand path
        """

        fn = self._self_add_one()

        class C(int):
            method = demand(fn)

        self.assertEqual(getattr(C(41), "method")(), 42)


    def test_demand_bound_method_via_getattr(self):
        """
        getattr(instance, method) on Demand-wrapped KosFunction binds self
        """

        fn = self._self_add_one()

        class C(int):
            method = demand(fn)

        self.assertEqual(demand_call(getattr(C(41), "method")), 42)


# The end.
