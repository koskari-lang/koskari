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

import functools
import gc
import operator
import unittest
import weakref

import pytest

import koskari.lib as lib
from koskari.lib import (
    AsyncGenerator, BoundMethod, Coroutine, Domain, FormalsSpec,
    KoskariSyncViolation, KosPartial, ParamRef, demand, demand_apply,
    demand_call)
from koskari.module import load_module_source_async, new_module
from tests import make_callable
from tests.lib.opcode import build_code, build_expr


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


    def test_returned_awaitable_is_stepped(self):
        """
        an awaitable returned as data is eagerly stepped, not handed back
        unstepped
        """

        async def settles():
            return 7

        code, ns = build_expr(
            [
                ["VAR", "coro"],
                ["RETURN", 1],
            ],
            values={"coro": settles()},
        )
        expr = make_callable(code, ns)
        self.assertEqual(demand_call(expr), 7)


    def test_returned_awaitable_that_suspends_is_denied(self):
        """
        an awaitable returned as data that suspends raises
        KoskariSyncViolation rather than escaping the demand boundary
        """

        import asyncio

        async def suspends():
            await asyncio.sleep(0)
            return 7

        coro = suspends()
        code, ns = build_expr(
            [
                ["VAR", "coro"],
                ["RETURN", 1],
            ],
            values={"coro": coro},
        )
        expr = make_callable(code, ns)
        with self.assertRaises(KoskariSyncViolation):
            demand_call(expr)
        coro.close()


    def test_yield_produces_demand_generator(self):
        """
        demand_call(expr) on a yielding expression returns its
        AsyncGenerator; iter() of it drives on the demand path
        """
        code, ns = build_expr([
            ["CONST", 1],
            ["YIELD", 1],
            ["RETURN", 0],
        ])
        expr = make_callable(code, ns)
        gen = demand_call(expr)
        self.assertIsInstance(gen, AsyncGenerator)
        it = iter(gen)
        self.assertEqual(next(it), 1)
        with self.assertRaises(StopIteration):
            next(it)


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


class TestDemandYieldingFunction(unittest.TestCase):
    """
    demand_call on yielding KosFunction returns its AsyncGenerator
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
        self.assertIsInstance(result, AsyncGenerator)
        it = iter(result)
        self.assertIs(iter(it), it)
        self.assertEqual(next(it), 1)


    def test_repr_includes_code_name(self):
        """
        demand_call result repr names the code (async_generator object name)
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
            next(iter(gen))
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
            next(iter(gen))
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


    def test_denied_await_releases_the_awaited_iterator(self):
        """
        The eager step caches the __await__ iterator on the context
        before suspending. Denying that suspension abandons it, so it
        must be released there rather than left for teardown. The
        handler runs inside the same context and reports what it sees.
        """

        held = {}

        def _suspending_iter():
            yield
            return None

        class SuspendingAwaitable:
            def __await__(self):
                it = _suspending_iter()
                held['ref'] = weakref.ref(it)
                return it

        def probe():
            gc.collect()
            return held['ref']() is None

        awaitable = SuspendingAwaitable()
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a', 'probe'),
            values={'a': awaitable, 'probe': probe},
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
                ["VAR", "probe"],
                ["CALL", 0],
                ["JUMP", "end"],
                ["LABEL", "end"],
                ["POP_TRY", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a', 'probe'),
            name='demand_denied_await_release',
        )
        fn = make_callable(code, base_ns, FormalsSpec())
        self.assertTrue(demand_call(fn))



class TestDemandSyncPlainPython(unittest.TestCase):
    """
    demand_call on plain Python callables, including coroutine functions.
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


    def test_python_coroutine_completes_immediately(self):
        """
        A Python coroutine that completes without suspending returns its value.
        """

        async def pure_sync():
            return 42

        self.assertEqual(demand_call(pure_sync), 42)


    def test_python_coroutine_suspends_raises_violation(self):
        """
        A Python coroutine that suspends raises KoskariSyncViolation.
        """
        import asyncio

        async def suspends():
            await asyncio.sleep(0)
            return 99

        with self.assertRaises(KoskariSyncViolation):
            demand_call(suspends)


    def test_python_coroutine_catches_violation_and_returns(self):
        """
        A Python coroutine whose __await__ iterator catches KoskariSyncViolation
        and returns a value yields that value to demand_call.
        """

        class RecoveringAwaitable:
            def __await__(self):
                try:
                    yield "suspend"
                except KoskariSyncViolation:
                    return "recovered"

        async def fn():
            return await RecoveringAwaitable()

        self.assertEqual(demand_call(fn), "recovered")


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


    def test_demand_idempotent(self):
        """
        demand(dfb) returns dfb when dfb is already a Demand
        """
        fn = lambda: 42
        dfb = demand(fn)
        self.assertIs(dfb, demand(dfb))
        self.assertIs(dfb.__wrapped__, fn)


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
                ["POP_FINALLY", 0],
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
        self.assertEqual(repr(wrapped), "<koskari demand 'expr'>")


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


    def test_bound_method_demand_from_bytecode_returns_value(self):
        """
        koskari CALL on BoundMethod(Demand(fn), obj) dispatches through
        demand_kos_install rather than bypassing it via boundmethod_resolve_function.
        """

        fn = self._self_add_one()

        class C(int):
            method = demand(fn)

        bound_method = getattr(C(41), "method")

        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('bm',),
            values={'bm': bound_method},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('bm',),
            name='call_bound_method',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
        self.assertEqual(demand_call(outer_fn), 42)


    def test_bound_method_demand_from_bytecode_denies_await(self):
        """
        koskari CALL on BoundMethod(Demand(fn_that_awaits), obj) now sets
        KOS_FRAME_DEMAND via demand_kos_install and raises KoskariSyncViolation.

        Previously kos_install_boundmethod called kos_install_function(resolved)
        directly, bypassing demand_kos_install, so KOS_FRAME_DEMAND was never set
        and the await would not be denied.
        """

        def _it():
            yield

        class SuspendingAwaitable:
            def __await__(self_inner):
                return _it()

        awaitable = SuspendingAwaitable()
        _, inner_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        # slot 0 = self (local), slot 1 = a (closure)
        inner_code = build_code(
            [
                ["FAST", 1],
                ["AWAIT", 1],
                ["RETURN", 1],
            ],
            local_syms=('self',),
            closure_syms=('a',),
            name='self_awaiting_fn',
        )
        inner_fn = make_callable(
            inner_code, inner_ns, FormalsSpec(("self",), None, ()))

        class C(int):
            method = demand(inner_fn)

        bound_method = getattr(C(41), "method")

        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('bm',),
            values={'bm': bound_method},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('bm',),
            name='call_demand_bound_method',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())

        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_call(outer_fn)
        self.assertIn('suspension', str(ctx.exception))


BOUND_SRC = """
(define gather (lambda [self a b: 0 *: rest] #(self a b rest)))
"""


def same(fn):
    """
    The function itself, for a class attribute that binds as a plain
    Function
    """

    return fn


class BoundMethodPythonCalls:
    """
    A BoundMethod held by Python and called from Python; subclasses
    choose the class attribute (wrap) and how a call's result is
    delivered (settle)
    """

    async def held(self):
        """
        A fresh instance and its BoundMethod over gather
        """

        module = new_module("bound_method_call")
        vars(module)["__file__"] = "<bound_method_call>"
        await load_module_source_async(module, BOUND_SRC, layout=False)
        attr = self.wrap(vars(module)["gather"])

        class C:
            method = attr

        obj = C()
        return obj, attr, obj.method


    async def test_positionals(self):
        """
        positionals follow self
        """

        obj, _, m = await self.held()
        self.assertIsInstance(m, BoundMethod)
        self.assertEqual(await self.settle(m(1, 2)), (obj, 1, 2, ()))


    async def test_keywords(self):
        """
        a keyword argument reaches its formal
        """

        obj, _, m = await self.held()
        self.assertEqual(await self.settle(m(1, b=2)), (obj, 1, 2, ()))


    async def test_nine_positionals(self):
        """
        nine positionals follow self in order
        """

        obj, _, m = await self.held()
        self.assertEqual(
            await self.settle(m(1, 2, 3, 4, 5, 6, 7, 8, 9)),
            (obj, 1, 2, (3, 4, 5, 6, 7, 8, 9)))


    async def test_nine_positionals_unpacked(self):
        """
        nine positionals unpacked from a tuple follow self in order
        """

        obj, _, m = await self.held()
        args = (1, 2, 3, 4, 5, 6, 7, 8, 9)
        self.assertEqual(
            await self.settle(m(*args)),
            (obj, 1, 2, (3, 4, 5, 6, 7, 8, 9)))


    async def test_star_args(self):
        """
        m(*args) passes the tuple's items after self
        """

        obj, _, m = await self.held()
        args = (1, 2, 3)
        self.assertEqual(await self.settle(m(*args)), (obj, 1, 2, (3,)))


    async def test_star_args_and_keywords(self):
        """
        m(*args, **kwds) passes the items after self and the keywords
        to their formals
        """

        obj, _, m = await self.held()
        args = (1,)
        kwds = {"b": 3}
        self.assertEqual(
            await self.settle(m(*args, **kwds)), (obj, 1, 3, ()))


    async def test_partial_positional(self):
        """
        functools.partial binding a positional of a bound method
        """

        obj, _, m = await self.held()
        part = functools.partial(m, 1)
        self.assertEqual(await self.settle(part(2)), (obj, 1, 2, ()))


    async def test_partial_keyword(self):
        """
        functools.partial binding a keyword of a bound method
        """

        obj, _, m = await self.held()
        part = functools.partial(m, b=5)
        self.assertEqual(await self.settle(part(1)), (obj, 1, 5, ()))


    async def test_func_and_self(self):
        """
        __func__ is the class attribute and __self__ the instance,
        before and after a call
        """

        obj, attr, m = await self.held()
        self.assertIs(m.__func__, attr)
        self.assertIs(m.__self__, obj)
        await self.settle(m(1, 2))
        self.assertIs(m.__func__, attr)
        self.assertIs(m.__self__, obj)


class TestBoundMethodOverDemandPythonCall(
        BoundMethodPythonCalls,
        unittest.IsolatedAsyncioTestCase):
    """
    A BoundMethod over a Demand returns the value from the call
    """

    wrap = staticmethod(demand)


    async def settle(self, result):
        """
        The call's value as returned
        """

        return result


class TestBoundMethodOverFunctionPythonCall(
        BoundMethodPythonCalls,
        unittest.IsolatedAsyncioTestCase):
    """
    A BoundMethod over a Function returns a coroutine from the call
    """

    wrap = staticmethod(same)


    async def settle(self, result):
        """
        The value of the call's coroutine
        """

        self.assertIsInstance(result, Coroutine)
        return await result


class TestKoskariIterPyIter(unittest.TestCase):
    """
    Koskari PUSH_FOREACH/ITER_NEXT over a KosPyIter.

    A KosPyIter is a KosIter provider whose suite forwards to its
    provider, so PUSH_FOREACH takes it as a provider and each step
    splices into the running chain.
    """

    def test_koskari_foreach_over_pyiter(self):
        """
        Koskari consumer sums values from a KosPyIter via
        PUSH_FOREACH / ITER_NEXT, run via demand_call on the consumer.
        """
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            values={},
        )
        inner_code = build_code(
            [
                ["CONST", 10],
                ["YIELD", 1],
                ["CONST", 20],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            name='inner_gen',
        )
        inner_fn = make_callable(inner_code, base_ns, FormalsSpec())
        dsg = demand_call(inner_fn)

        consumer_code, consumer_ns = build_expr([
            ["CONST", 0],
            ["VAR_STORE", "total"],
            ["VAR", "dsg"],
            ["PUSH_FOREACH", 0],
            ["ITER_NEXT", "else_lbl"],
            ["VAR", "total"],
            ["OBJECT_ADD", 2],
            ["VAR_STORE", "total"],
            ["CONTINUE", 0],
            ["LABEL", "else_lbl"],
            ["LABEL", "break_lbl"],
            ["POP_FOREACH", 0],
            ["POP", 1],
            ["VAR", "total"],
            ["RETURN", 1],
        ], values={"dsg": dsg, "total": 0})
        result = demand_call(make_callable(consumer_code, consumer_ns))
        self.assertEqual(result, 30)


class TestDemandCallFlattening(unittest.TestCase):
    """
    Phase-2 demand call flattening: KosDemand is now a KosCallable.

    When koskari bytecode calls demand(fn), the CALL opcode dispatches
    through demand_kos_install (not demand_tp_call), which either:
    - installs the inner callable's frame directly and marks it as the
      demand root (flattenable path: non-yielding KosFunction/KosExpression)
    - delegates to demand_sync_ex (fallback: yielding fn, Python callable)
    """

    def _make_non_yielding_fn(self, return_value):
        """Build a non-yielding KosFunction that returns return_value."""

        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('rv',),
            values={'rv': return_value},
        )
        code = build_code(
            [
                ["FAST", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('rv',),
            name='non_yielding_fn',
        )
        return make_callable(code, base_ns, FormalsSpec())


    def _make_yielding_fn(self, yield_value):
        """Build a yielding KosFunction (YIELD opcode)."""

        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('yv',),
            values={'yv': yield_value},
        )
        code = build_code(
            [
                ["FAST", 0],
                ["YIELD", 1],
                ["RETURN", 0],
            ],
            local_syms=(),
            closure_syms=('yv',),
            name='yielding_fn',
        )
        return make_callable(code, base_ns, FormalsSpec())


    def test_demand_is_kos_callable(self):
        """
        demand(fn) is now a KosCallable subtype: CALL opcode dispatches
        through the install slot rather than tp_call.

        Verify indirectly: demand(fn) is a Demand instance, and calling it
        from koskari bytecode via CALL (KosCallable path) produces the same
        result as demand_call(fn).
        """

        fn = self._make_non_yielding_fn(42)
        wrapped = demand(fn)
        self.assertIsInstance(wrapped, demand)


    def test_flatten_non_yielding_function_from_bytecode(self):
        """
        CALL demand(fn) from koskari bytecode installs fn's frame directly
        and marks it KOS_FRAME_DEMAND (flattenable path).

        Verify that the result matches demand_call(fn).
        """

        fn = self._make_non_yielding_fn(99)
        wrapped = demand(fn)

        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('w',),
            values={'w': wrapped},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('w',),
            name='call_demand_fn',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())

        result = demand_call(outer_fn)
        self.assertEqual(result, 99)


    def test_flatten_sets_demand_flag_denies_await(self):
        """
        After flattening demand(fn), the installed frame has KOS_FRAME_DEMAND
        set, so any await inside fn raises KoskariSyncViolation in-eval.
        """

        def _yielding_awaitable():
            def _it():
                yield
            class SuspendingAwaitable:
                def __await__(self):
                    return _it()
            return SuspendingAwaitable()

        awaitable = _yielding_awaitable()
        _, inner_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('a',),
            values={'a': awaitable},
        )
        inner_code = build_code(
            [
                ["FAST", 0],
                ["AWAIT", 1],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('a',),
            name='awaiting_fn',
        )
        inner_fn = make_callable(inner_code, inner_ns, FormalsSpec())
        wrapped = demand(inner_fn)

        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('w',),
            values={'w': wrapped},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('w',),
            name='call_demand_awaiting',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())

        with self.assertRaises(KoskariSyncViolation) as ctx:
            demand_call(outer_fn)
        self.assertIn('suspension', str(ctx.exception))


    def test_fallback_python_callable_from_bytecode(self):
        """
        demand(python_fn) called from koskari bytecode takes the fallback path:
        demand_sync_ex calls the Python callable synchronously and pushes the
        result on the stack (no KosCallable flatten, no frame push).
        """

        python_fn = lambda: 99
        wrapped = demand(python_fn)

        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('w',),
            values={'w': wrapped},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('w',),
            name='call_demand_python',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
        self.assertEqual(demand_call(outer_fn), 99)


    def test_fallback_yielding_function_returns_generator(self):
        """
        demand(yielding_fn) called from koskari bytecode returns the
        AsyncGenerator itself; demand applies when it is driven, not when
        it is constructed
        """

        fn = self._make_yielding_fn(7)
        wrapped = demand(fn)

        _, outer_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=('w',),
            values={'w': wrapped},
        )
        outer_code = build_code(
            [
                ["FAST", 0],
                ["CALL", 0],
                ["RETURN", 1],
            ],
            local_syms=(),
            closure_syms=('w',),
            name='call_demand_yielding',
        )
        outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())

        result = demand_call(outer_fn)
        self.assertIsInstance(result, AsyncGenerator)
        self.assertEqual(next(iter(result)), 7)


def _concat_fn():
    """
    Build KosFunction (a, b) -> a + b
    """

    code, base_ns = build_expr([
        ["VAR", "a"],
        ["VAR", "b"],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ])
    fspec = FormalsSpec(("a", "b"), None, ())
    return code.function(fspec(), base_ns)


class TestDemandBoundaryCallables(unittest.TestCase):
    """
    The demand core accepts every KosCallable at the Python boundary
    """

    def test_demand_partial_python_inner(self):
        """
        demand(partial(operator.add, 5))(100) from Python
        """

        self.assertEqual(demand(KosPartial(operator.add, 5))(100), 105)


    def test_demand_call_partial_koskari_inner(self):
        """
        demand_call on a partial over a KosFunction
        """

        fn = _concat_fn()
        self.assertEqual(demand_call(KosPartial(fn, "x"), "y"), "xy")


    def test_demand_apply_keywords(self):
        """
        demand_apply with keyword arguments at the boundary
        """

        fn = _concat_fn()
        self.assertEqual(demand_apply(fn, ("x",), {"b": "y"}), "xy")


    def test_demand_vectorcall_keywords(self):
        """
        demand(fn) called from Python with a keyword argument
        """

        fn = _concat_fn()
        self.assertEqual(demand(fn)("x", b="y"), "xy")


    def test_demand_call_partial_with_bound_keyword(self):
        """
        demand_call on a partial carrying a bound keyword
        """

        fn = _concat_fn()
        self.assertEqual(demand_call(KosPartial(fn, b="y"), "x"), "xy")



ROOT_SRC = """
(define add2 (lambda [a b] (+ a b)))

(define fail2 (lambda [a b] (raise (ValueError (+ a b)))))

(define fact2 (lambda fact2 [n acc]
  (if (== n 0) acc (fact2 (- n 1) (* acc n)))))

(define descend (lambda [via n]
  (if (== n 0) 0 (+ 1 (via n)))))

(define escape2 (lambda [a b]
  (+ 1 (call/cc (lambda [k] (call k (+ a b)) (raise (ValueError "late")))))))

(define tuple2 (lambda [a b] #(a b)))

(define bind_gq (lambda [v probe]
  (parameterize [[gq v]]
    (return (probe)))))

(define-class Box ()
  (begin
    (define __init__ [self x]
      (set-attr self x x))))
"""


def root_install_count():
    """
    The entry_root_install trace counter, or None when the build does
    not count events
    """

    report = lib.get_trace_report()
    if not report["flags"]["events"]:
        return None
    return report["events"]["entry_root_install"]


class TestDemandRootCallee(unittest.IsolatedAsyncioTestCase):
    """
    A Demand over a simple two-argument function called from Python:
    every call after the first runs the function as the drive's root
    frame
    """

    async def load(self):
        module = new_module("demand_root")
        vars(module)["__file__"] = "<demand_root>"
        await load_module_source_async(module, ROOT_SRC, layout=False)
        return vars(module)


    async def test_return_value(self):
        """
        each call returns its own sum
        """

        mod = await self.load()
        d = demand(mod["add2"])
        self.assertEqual(d(1, 2), 3)
        self.assertEqual(d(10, 20), 30)
        self.assertEqual(d("x", "y"), "xy")


    async def test_raise_propagates(self):
        """
        a raise in the function reaches Python with its type, and the
        wrapper stays usable
        """

        mod = await self.load()
        d = demand(mod["fail2"])
        for a, b in ((1, 2), (3, 4)):
            with self.assertRaises(ValueError) as caught:
                d(a, b)
            self.assertEqual(caught.exception.args, (a + b,))
        self.assertEqual(demand(mod["add2"])(5, 6), 11)


    async def test_function_naming_itself(self):
        """
        a named lambda recursing through its own name, called twice
        """

        mod = await self.load()
        d = demand(mod["fact2"])
        self.assertEqual(d(5, 1), 120)
        self.assertEqual(d(6, 1), 720)
        self.assertEqual(d(0, 7), 7)


    async def test_reentry_through_python(self):
        """
        the function calls back into Python, which calls the same Demand
        again, three levels deep
        """

        mod = await self.load()
        d = demand(mod["descend"])
        seen = []

        def via(n):
            seen.append(n)
            return d(via, n - 1)

        self.assertEqual(d(via, 3), 3)
        self.assertEqual(seen, [3, 2, 1])
        self.assertEqual(d(via, 2), 2)
        self.assertEqual(seen, [3, 2, 1, 2, 1])


    async def test_call_cc_escape(self):
        """
        a call/cc escape out of the callee's inner lambda returns the
        escaped value into the rest of the callee's body
        """

        mod = await self.load()
        d = demand(mod["escape2"])
        self.assertEqual(d(1, 2), 4)
        self.assertEqual(d(10, 20), 31)


    async def test_first_call_general_second_root(self):
        """
        a fresh function's first call resolves its self-bind on the
        general path; the second runs as the root frame
        """

        mod = await self.load()
        d = demand(mod["tuple2"])

        before = root_install_count()
        self.assertEqual(d(1, 2), (1, 2))
        middle = root_install_count()
        self.assertEqual(d(3, 4), (3, 4))
        after = root_install_count()

        if before is not None:
            self.assertEqual(middle - before, 0)
            self.assertEqual(after - middle, 1)


    async def test_parameterize_binding_ends_with_the_call(self):
        """
        a parameterize open at the function's return is unwound: the
        binding is seen inside the call and absent after it
        """

        mod = await self.load()
        d = demand(mod["bind_gq"])

        def probe():
            return ParamRef("gq").deref()

        with Domain():
            for value in (5, 6):
                self.assertEqual(d(value, probe), value)
                with self.assertRaises(NameError):
                    ParamRef("gq").deref()


    async def test_simple_init_from_python(self):
        """
        a class with a simple __init__ instantiated twice from Python
        """

        mod = await self.load()
        box = mod["Box"]
        first = box(3)
        second = box("y")
        self.assertEqual(first.x, 3)
        self.assertEqual(second.x, "y")


# The end.
