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
Tests for KosMap over several sources: func takes one element from
each iterable per step, left to right, and the shortest iterable ends
the map, as builtins.map does

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import unittest

import pytest

from koskari.lib import FormalsSpec, KosMap, KosPartial
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


async def agen(items):
    """
    An async iterator that suspends before each element
    """

    for item in items:
        await asyncio.sleep(0)
        yield item


class Recording:
    """
    A sync iterator that records how many times it was asked
    """

    def __init__(self, items):
        self.it = iter(items)
        self.pulls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.pulls += 1
        return next(self.it)


class ClosingSource:
    """
    A sync iterator recording whether its close was reached
    """

    def __init__(self, items):
        self.it = iter(items)
        self.closed = False

    def __iter__(self):
        return self

    def __next__(self):
        return next(self.it)

    def close(self):
        self.closed = True


def add(a, b):
    return a + b


def add3(a, b, c):
    return a + b + c


def koskari_binary(fn):
    """
    A koskari function of two parameters that returns fn applied to
    them, so the map's step installs a frame rather than calling into
    Python
    """

    code, base_ns = build_expr([
        ["CONST", fn],
        ["FAST", 0],
        ["FAST", 1],
        ["CALL", 2],
        ["RETURN", 1],
    ], local_syms=("x", "y"))
    return code.function(FormalsSpec(("x", "y"), None, ())(), base_ns)


class TestKosMapSourcesSync(unittest.TestCase):
    """
    Every source stepping through tp_iternext and a plain python
    function: the whole step runs without the evaluator
    """

    def test_two_sources(self):
        self.assertEqual(list(KosMap(add, [1, 2, 3], [10, 20, 30])),
                         [11, 22, 33])

    def test_three_sources(self):
        self.assertEqual(list(KosMap(add3, [1, 2], [10, 20], [100, 200])),
                         [111, 222])

    def test_matches_builtins_map(self):
        rows = [range(i, i + 5) for i in range(4)]
        self.assertEqual(list(KosMap(max, *rows)), list(map(max, *rows)))

    def test_first_source_shorter_ends_the_map(self):
        self.assertEqual(list(KosMap(add, [1, 2], [10, 20, 30])), [11, 22])

    def test_second_source_shorter_ends_the_map(self):
        self.assertEqual(list(KosMap(add, [1, 2, 3], [10, 20])), [11, 22])

    def test_later_sources_not_asked_once_an_earlier_one_is_spent(self):
        first = Recording([1, 2])
        second = Recording([10, 20, 30])
        self.assertEqual(list(KosMap(add, first, second)), [11, 22])
        self.assertEqual(first.pulls, 3)
        self.assertEqual(second.pulls, 2)

    def test_earlier_sources_asked_on_the_ending_row(self):
        first = Recording([1, 2, 3])
        second = Recording([10, 20])
        self.assertEqual(list(KosMap(add, first, second)), [11, 22])
        self.assertEqual(first.pulls, 3)
        self.assertEqual(second.pulls, 3)

    def test_exhausted_map_stays_exhausted(self):
        m = KosMap(add, [1], [2])
        self.assertEqual(list(m), [3])
        self.assertEqual(list(m), [])

    def test_elements_are_not_held_past_the_map(self):
        import weakref

        class Box:
            pass

        box = Box()
        ref = weakref.ref(box)
        m = KosMap(lambda a, b: None, [box], [1])
        self.assertEqual(list(m), [None])
        del box
        del m
        self.assertIsNone(ref())

    def test_elements_are_not_held_past_their_step(self):
        import weakref

        class Box:
            pass

        refs = []

        class Boxes:
            def __iter__(self):
                return self

            def __next__(self):
                if len(refs) == 2:
                    raise StopIteration
                box = Box()
                refs.append(weakref.ref(box))
                return box

        m = KosMap(lambda a, b: None, Boxes(), [1, 2])
        it = iter(m)
        next(it)
        self.assertIsNone(refs[0]())
        self.assertIsNone(next(it))

    def test_partial_row_is_not_held_at_exhaustion(self):
        import weakref

        class Box:
            pass

        refs = []

        class Boxes:
            def __iter__(self):
                return self

            def __next__(self):
                if len(refs) == 2:
                    raise StopIteration
                box = Box()
                refs.append(weakref.ref(box))
                return box

        m = KosMap(lambda a, b: None, Boxes(), [1])
        it = iter(m)
        next(it)
        with self.assertRaises(StopIteration):
            next(it)
        self.assertIsNone(refs[1]())

    def test_row_stays_visible_to_the_collector(self):
        """
        a collection while the row holds only atoms untracks it; the
        next fill tracks it again, so a cycle through the row is found
        """
        import gc
        import weakref

        class Box:
            pass

        box = Box()
        ref = weakref.ref(box)
        m = KosMap(lambda a, b: None, iter([1, box]), iter([2, 3]))
        it = iter(m)
        next(it)
        gc.collect()
        next(it)
        box.m = m
        del box, m, it
        gc.collect()
        self.assertIsNone(ref())

    def test_func_raising_stop_iteration_truncates(self):
        def stop_at(a, b):
            if a == 2:
                raise StopIteration
            return a + b

        self.assertEqual(list(KosMap(stop_at, [1, 2, 3], [10, 20, 30])),
                         [11])

    def test_partial_over_two_sources(self):
        """
        a KosCallable that is not direct-delivery is staged and called
        from the completed row
        """

        fn = KosPartial(add3, 100)
        self.assertEqual(list(KosMap(fn, [1, 2], [10, 20])), [111, 122])

    def test_one_source_still_the_same_type(self):
        m = KosMap(abs, [-1, -2])
        self.assertIsInstance(m, KosMap)
        self.assertEqual(list(m), [1, 2])
        self.assertIn("map", repr(m))


class TestKosMapSourcesKoskariFunction(unittest.TestCase):
    """
    A koskari function of two parameters installs a frame per step
    with the row as its argument vector
    """

    def test_two_sources(self):
        fn = koskari_binary(add)
        self.assertEqual(list(KosMap(fn, [1, 2, 3], [10, 20, 30])),
                         [11, 22, 33])

    def test_shortest_ends_the_map(self):
        fn = koskari_binary(add)
        self.assertEqual(list(KosMap(fn, [1, 2, 3], [10])), [11])


class TestKosMapSourcesConstruction(unittest.TestCase):
    """
    Sources are acquired eagerly, so a bad argument raises where the
    map is written rather than inside whatever drives it
    """

    def test_no_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosMap()

    def test_func_alone_refused(self):
        with self.assertRaises(TypeError):
            KosMap(abs)

    def test_non_callable_func_refused(self):
        with self.assertRaises(TypeError):
            KosMap(5, [1], [2])

    def test_non_iterable_source_refused(self):
        with self.assertRaises(TypeError):
            KosMap(add, [1, 2], 5)

    def test_keyword_arguments_refused(self):
        with self.assertRaises(TypeError):
            KosMap(add, [1, 2], other=[3, 4])


class TestKosMapSourcesDriven(unittest.IsolatedAsyncioTestCase):
    """
    A driven source anywhere in the row, or a function that awaits, is
    what builtins.map cannot consume at all
    """

    async def collect(self, m):
        return [v async for v in m]

    async def test_sync_with_async_source(self):
        self.assertEqual(await self.collect(KosMap(add, [1, 2, 3],
                                                   agen([10, 20, 30]))),
                         [11, 22, 33])

    async def test_async_with_sync_source(self):
        self.assertEqual(await self.collect(KosMap(add, agen([1, 2, 3]),
                                                   [10, 20, 30])),
                         [11, 22, 33])

    async def test_all_async_sources(self):
        self.assertEqual(await self.collect(KosMap(add3, agen([1, 2]),
                                                   agen([10, 20]),
                                                   agen([100, 200]))),
                         [111, 222])

    async def test_middle_async_source_of_three(self):
        self.assertEqual(await self.collect(KosMap(add3, [1, 2],
                                                   agen([10, 20]),
                                                   [100, 200])),
                         [111, 222])

    async def test_async_source_ends_the_map(self):
        self.assertEqual(await self.collect(KosMap(add, [1, 2, 3],
                                                   agen([10]))),
                         [11])

    async def test_awaiting_function(self):
        async def slow_add(a, b):
            await asyncio.sleep(0)
            return a + b

        self.assertEqual(await self.collect(KosMap(slow_add, [1, 2],
                                                   [10, 20])),
                         [11, 22])

    async def test_awaiting_function_over_async_sources(self):
        async def slow_add(a, b):
            await asyncio.sleep(0)
            return a + b

        self.assertEqual(await self.collect(KosMap(slow_add, agen([1, 2]),
                                                   agen([10, 20]))),
                         [11, 22])

    async def test_koskari_function_over_async_sources(self):
        fn = koskari_binary(add)
        self.assertEqual(await self.collect(KosMap(fn, agen([1, 2]),
                                                   agen([10, 20]))),
                         [11, 22])

    async def test_concurrent_anext_refused_while_suspended(self):
        release = asyncio.Event()

        async def slow_add(a, b):
            await release.wait()
            return a + b

        m = KosMap(slow_add, [1, 2], [10, 20])
        first = asyncio.ensure_future(m.__anext__())
        await asyncio.sleep(0)

        with self.assertRaises(ValueError):
            await m.__anext__()

        release.set()
        self.assertEqual(await first, 11)


class TestKosMapSourcesClose(unittest.IsolatedAsyncioTestCase):
    """
    Closing the map closes every source
    """

    async def test_aclose_reaches_every_source(self):
        first = ClosingSource([1, 2, 3])
        second = ClosingSource([10, 20, 30])
        m = KosMap(add, first, second)
        self.assertEqual(await m.__anext__(), 11)
        await m.aclose()
        self.assertTrue(first.closed)
        self.assertTrue(second.closed)

    async def test_closed_map_is_exhausted(self):
        m = KosMap(add, [1, 2], [10, 20])
        await m.aclose()
        with self.assertRaises(StopAsyncIteration):
            await m.__anext__()


# The end.
