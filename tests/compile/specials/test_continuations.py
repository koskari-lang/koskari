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
Tests for continuation special forms and delimited transfer policy.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

import asyncio
import gc
import weakref

from koskari.exceptions import KoskariPromptUnreachable

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestDelimitedTransferNested(SpecialsTestCase):
    """
    A delimited invoke from inside a wind thunk that another delimited
    invoke is entering attaches a second slice above the first; the inner
    invoke returns its reset's value to the thunk, and the outer invoke
    then finishes entering and runs its own slice.
    """

    async def test_nested_invoke_from_before_thunk_during_delimited_push(self):
        """
        dyn-wind wraps shift so the wind is captured with the prompt. The
        pass counter invokes k from the cloned wind's before-thunk exactly
        once, on the first re-entry, so the recursion is bounded: the inner
        invoke resumes the slice with 99 and returns 99 to the thunk, and
        the outer invoke resumes the slice with 0
        """

        src_str = """
        (let [[pass 0] [k nil]]
          (begin
            (setq k
              (reset
                (dyn-wind
                  (begin
                    (setq pass (+ pass 1))
                    (cond
                      ((== pass 2) (call k 99))
                      (else: nil)))
                  (shift k k)
                  nil)))
            (call record (call k 0))
            pass))
        """
        results = []
        expr, mod = self.compile_expr(src_str, record=results.append)
        passes = await expr()
        self.assertEqual(results, [0])
        self.assertEqual(passes, 3)


    async def test_unbounded_nested_invoke_raises_recursion_error(self):
        """
        every re-entry of the cloned before-thunk invokes k again, so
        each pass attaches another slice above the last. Each attachment adds
        frames, so the frame depth limit refuses an invoke once the
        chain is as deep as the limit and RecursionError raises
        at that invoke site instead of the chain growing without bound
        """

        src_str = """
        (let [[pass 0] [k nil]]
          (begin
            (setq k
              (reset
                (dyn-wind
                  (begin
                    (setq pass (+ pass 1))
                    (cond
                      ((> pass 1) (call k 99))
                      (else: nil)))
                  (shift k k)
                  nil)))
            (call k 0)))
        """
        expr, mod = self.compile_expr(src_str)
        with self.assertRaises(RecursionError):
            await expr()


class TestUndelimitedSeam(SpecialsTestCase):
    """
    An undelimited invoke leaves and re-enters only the extents between
    the invoke point and the lowest common ancestor of the live chain
    and the captured one. A dyn-wind, parameterize, or with that both
    points sit inside is the same extent and runs no thunk; the operands
    those shared nodes held at capture come back.
    """

    def wind_source(self, limit):
        return f"""
        (let [[saved nil] [n 0]]
          (dyn-wind
            (call record "before")
            (begin
              (let/cc k (setq saved k))
              (setq n (+ n 1))
              (call record (+ "body" (str n)))
              (cond ((< n {limit}) (call saved 0)) (else: nil))
              n)
            (call record "after")))
        """


    async def test_shared_wind_runs_no_thunk(self):
        """
        capture and invoke inside the same dyn-wind body: neither thunk
        runs on the re-entry
        """

        log = []
        expr, mod = self.compile_expr(
            self.wind_source(3), record=log.append, str=str)
        result = await expr()
        self.assertEqual(result, 3)
        self.assertEqual(
            log, ["before", "body1", "body2", "body3", "after"])


    async def test_unshared_wind_above_shared_one_fires_alone(self):
        """
        a dyn-wind entered after the capture leaves on the invoke; the
        one both points sit inside does not
        """

        src_str = """
        (let [[saved nil] [n 0]]
          (dyn-wind
            (call record "b1")
            (begin
              (let/cc k (setq saved k))
              (setq n (+ n 1))
              (cond
                ((< n 2)
                 (dyn-wind
                   (call record "b2")
                   (call saved 0)
                   (call record "a2")))
                (else: nil))
              n)
            (call record "a1")))
        """
        log = []
        expr, mod = self.compile_expr(src_str, record=log.append)
        result = await expr()
        self.assertEqual(result, 2)
        self.assertEqual(log, ["b1", "b2", "a2", "a1"])


    async def test_attached_clone_counts_as_shared(self):
        """
        an invoke from outside a left dyn-wind attaches and enters it; a
        second invoke from inside that attached body finds it shared and
        runs no thunk
        """

        src_str = """
        (let [[saved nil] [n 0]]
          (begin
            (dyn-wind
              (call record "b")
              (begin
                (let/cc k (setq saved k))
                (setq n (+ n 1))
                (call record (+ "body" (str n)))
                (cond ((== n 2) (call saved 0)) (else: nil)))
              (call record "a"))
            (cond ((== n 1) (call saved 0)) (else: nil))
            n))
        """
        log = []
        expr, mod = self.compile_expr(src_str, record=log.append, str=str)
        result = await expr()
        self.assertEqual(result, 3)
        self.assertEqual(
            log, ["b", "body1", "a", "b", "body2", "body3", "a"])


    async def test_shared_parameterize_keeps_its_binding(self):
        """
        a parameterize both points sit inside is neither unwound nor
        re-bound; the binding is in force after every re-entry
        """

        from koskari.lib import ParamRef

        def read():
            return ParamRef('amt', 0).deref()

        src_str = """
        (let [[saved nil] [n 0]]
          (parameterize [[amt 7]]
            (begin
              (let/cc k (setq saved k))
              (setq n (+ n 1))
              (call record (call read))
              (cond ((< n 3) (call saved 0)) (else: nil))
              n)))
        """
        log = []
        expr, mod = self.compile_expr(src_str, record=log.append, read=read)
        result = await expr()
        self.assertEqual(result, 3)
        self.assertEqual(log, [7, 7, 7])


    async def test_shared_with_exits_once(self):
        """
        a with both points sit inside runs __enter__ once and __exit__
        once, at the end
        """

        log = []

        class Mgr:
            def __enter__(self):
                log.append("enter")
                return self
            def __exit__(self, *exc):
                log.append("exit")
                return False

        src_str = """
        (let [[saved nil] [n 0]]
          (with [(call mgr)]
            (begin
              (let/cc k (setq saved k))
              (setq n (+ n 1))
              (call record (+ "body" (str n)))
              (cond ((< n 3) (call saved 0)) (else: nil))
              n)))
        """
        expr, mod = self.compile_expr(
            src_str, mgr=Mgr, record=log.append, str=str)
        result = await expr()
        self.assertEqual(result, 3)
        self.assertEqual(
            log, ["enter", "body1", "body2", "body3", "exit"])


    async def test_shared_frame_operands_come_back(self):
        """
        the operands pending in the shared frame at capture, 10 here,
        are what the delivered value is pushed on, whatever the frame held
        at the invoke
        """

        src_str = """
        (let [[saved nil] [n 0] [acc nil]]
          (begin
            (setq acc (+ 10 (let/cc k (setq saved k) 1)))
            (call record acc)
            (setq n (+ n 1))
            (cond ((< n 3) (+ 100 (call saved (* n 100)))) (else: nil))
            acc))
        """
        log = []
        expr, mod = self.compile_expr(src_str, record=log.append)
        result = await expr()
        self.assertEqual(result, 210)
        self.assertEqual(log, [11, 110, 210])


class TestNestedUndelimitedVoidsOuter(SpecialsTestCase):
    """
    A goal waiting on a wind's after thunk is void once an inner
    undelimited invoke from that thunk unlinks the wind: the extent the
    outer was moving through is gone, so the outer must not resume on a
    later node that happens to sit where the wind was.
    """

    async def test_after_thunk_invoke_voids_the_waiting_outer(self):
        """
        the body invokes k; leaving runs the after thunk, which invokes
        k again; the second application wins and the first never delivers.
        The call to bump after the transfer allocates a frame where the
        unlinked wind stood and awaits in it, which is where a surviving
        outer would wrongly resume
        """

        src_str = """
        (let [[k nil] [n 0]]
          (let [[bump (lambda [] (begin (await (call pause)) (setq n (+ n 10))))]]
            (begin
              (let/cc c (setq k c))
              (setq n (+ n 1))
              (call record n)
              (cond
                ((== n 1)
                 (dyn-wind
                   None
                   (call k 1)
                   (call k 2)))
                (else: nil))
              (call bump)
              n)))
        """
        log = []
        expr, mod = self.compile_expr(
            src_str, record=log.append, pause=lambda: asyncio.sleep(0))
        result = await expr()
        self.assertEqual(log, [1, 2])
        self.assertEqual(result, 12)


    async def test_after_thunk_invoke_voids_outer_waiting_on_dyn_with(self):
        """
        same shape with the outer waiting on a dyn-with __exit__
        """

        seen = []

        class Mgr:
            def __enter__(self):
                seen.append("enter")
                return self
            def __exit__(self, *exc):
                seen.append("exit")
                return False

        src_str = """
        (let [[k nil] [n 0]]
          (let [[bump (lambda [] (begin (await (call pause)) (setq n (+ n 10))))]]
            (begin
              (let/cc c (setq k c))
              (setq n (+ n 1))
              (call record n)
              (cond
                ((== n 1)
                 (dyn-wind
                   None
                   (dyn-with [(call mgr)] (call k 1))
                   (call k 2)))
                (else: nil))
              (call bump)
              n)))
        """
        log = []
        expr, mod = self.compile_expr(
            src_str, record=log.append, mgr=Mgr,
            pause=lambda: asyncio.sleep(0))
        result = await expr()
        self.assertEqual(log, [1, 2])
        self.assertEqual(result, 12)
        self.assertEqual(seen, ["enter", "exit"])


class TestWaitingGoalThunkDeliveries(SpecialsTestCase):
    """
    While a goal waits on a wind's after thunk, the thunk is ordinary
    code: a native map over an async function that completes without
    suspending stages each result for the evaluator, and those staged
    deliveries must reach their resumes, not be cleared for a goal that
    is not about to drive.
    """

    async def test_map_over_async_in_after_thunk_during_transfer(self):
        from koskari.lib import KosMap

        async def adouble(x):
            return x * 2

        src_str = """
        (let [[k nil] [n 0]]
          (begin
            (let/cc c (setq k c))
            (setq n (+ n 1))
            (cond
              ((== n 1)
               (dyn-wind
                 None
                 (call k 1)
                 (for-each [x (call map adouble #[1 2 3])]
                   (call record x))))
              (else: nil))
            n))
        """
        log = []
        expr, mod = self.compile_expr(
            src_str, record=log.append, map=KosMap, adouble=adouble)
        result = await expr()
        self.assertEqual(log, [2, 4, 6])
        self.assertEqual(result, 2)


    async def test_shift_out_of_a_foreach_step_abandons_the_step(self):
        """
        A shift from inside a mapped function, under a reset outside
        the loop, abandons the step in flight: the reset's value is the
        handler's, the loop variable never receives it, and the map is
        finished so a second loop over it carries on from the next
        element
        """

        from koskari.lib import KosMap

        src_str = """
        (let [[m (KosMap (lambda [x] (shift k (* x 100))) #[1 2 3])]
              [seen #[]]]
          #((reset (for-each [x m] (seen.append x)))
            (reset (for-each [x m] (seen.append x)))
            seen))
        """
        expr, _mod = self.compile_expr(src_str, KosMap=KosMap)
        self.assertEqual(await expr(), (100, 200, []))


    async def test_shift_out_of_a_next_step_abandons_the_step(self):
        """
        A shift from inside a mapped function stepped by next, under a
        reset: the reset's value is the handler's, not the element
        """

        from koskari.lib import KosMap

        src_str = """
        (let [[m (KosMap (lambda [x] (shift k (* x 100))) #[1 2 3])]]
          (reset (next m)))
        """
        expr, _mod = self.compile_expr(src_str, KosMap=KosMap)
        self.assertEqual(await expr(), 100)


    async def test_shift_out_of_a_next_step_finishes_the_provider(self):
        """
        A shift from inside a mapped function stepped by next, under a
        reset, finishes the map as abandoned: a later next steps it
        again from the following element
        """

        from koskari.lib import KosMap

        src_str = """
        (let [[m (KosMap (lambda [x]
                           (cond
                             [(== x 1) (shift k (* x 100))]
                             [else: (* x 10)]))
                         #[1 2 3])]]
          #((reset (next m)) (next m)))
        """
        expr, _mod = self.compile_expr(src_str, KosMap=KosMap)
        self.assertEqual(await expr(), (100, 20))


    async def test_abandoned_next_step_releases_the_map(self):
        """
        A next step abandoned by a shift releases its reference to the
        map: once the caller drops its own, the map is collected, and
        with it the source only the map holds. The map takes no weak
        reference, so the source carries the weakref
        """

        from koskari.lib import KosMap

        class Source:
            def __init__(self, items):
                self.items = iter(items)


            def __iter__(self):
                return self


            def __next__(self):
                return next(self.items)

        source = Source([1, 2, 3])
        ref = weakref.ref(source)

        fn_expr, fn_mod = self.compile_expr(
            "(lambda [x] (shift k (* x 100)))")
        m = KosMap(await fn_expr(), source)
        del fn_expr, fn_mod, source

        expr, mod = self.compile_expr("(reset (next m))", m=m)
        self.assertEqual(await expr(), 100)

        del expr, mod, m
        gc.collect()
        self.assertIsNone(ref())


class TestCaptureRefusalsAreTyped(SpecialsTestCase):
    """
    A capture that cannot be taken raises KoskariPromptUnreachable
    naming what stood in the way: a generator, or a with or dyn-wind
    whose call is in flight. A with or dyn-wind at rest inside the
    region is captured. Never a bare RuntimeError
    """

    async def test_let_cc_inside_a_generator(self):
        expr, mod = self.compile_expr("((lambda [] (yield (let/cc k 2))))")
        gen = await expr()
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await gen.__anext__()
        self.assertEqual("generator", caught.exception.boundary)
        self.assertEqual("call/cc", caught.exception.operation)
        self.assertIsNone(caught.exception.tag)
        self.assertIsInstance(caught.exception, RuntimeError)


    async def test_call_cc_inside_a_running_exit(self):
        exit_expr, _ = self.compile_expr(
            "(lambda [self t v tb] (call/cc (lambda [k] (k 1))) False)")
        exit_fn = await exit_expr()

        class Mgr:

            def __enter__(self):
                return self

            __exit__ = exit_fn

        expr, mod = self.compile_expr(
            "(with [(call make_mgr)] 1)", make_mgr=Mgr)
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await expr()
        self.assertEqual("exit", caught.exception.boundary)
        self.assertEqual("call/cc", caught.exception.operation)


    async def test_call_cc_inside_a_running_after_thunk(self):
        src = """
        (dyn-wind
          nil
          4
          (call/cc (lambda [k] (k 1))))
        """
        expr, mod = self.compile_expr(src)
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await expr()
        self.assertEqual("after", caught.exception.boundary)
        self.assertEqual("call/cc", caught.exception.operation)


    async def test_shift_inside_a_running_exit_under_reset(self):
        exit_expr, _ = self.compile_expr(
            "(lambda [self t v tb] (shift k k) False)")
        exit_fn = await exit_expr()

        class Mgr:

            def __enter__(self):
                return self

            __exit__ = exit_fn

        expr, mod = self.compile_expr(
            "(reset (with [(call make_mgr)] 5))", make_mgr=Mgr)
        with self.assertRaises(KoskariPromptUnreachable) as caught:
            await expr()
        self.assertEqual("exit", caught.exception.boundary)
        self.assertEqual("shift", caught.exception.operation)
        self.assertIsNone(caught.exception.tag)


# The end.
