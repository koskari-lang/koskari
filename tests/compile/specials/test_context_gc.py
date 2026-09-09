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
A suspended evaluation context reports everything it holds to the
collector

A return that unwinds through a dyn-wind whose after thunk suspends on
a Python awaitable parks the coroutine with the return value pending on
its context. That value must be visible to the collector, or a cycle
through it never collects.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import gc
import weakref

from . import SpecialsTestCase

import pytest


pytestmark = pytest.mark.phase_specials


class Park:
    """
    An awaitable that yields once, then completes on the next send
    """

    def __await__(self):
        yield self
        return "parked"


class Box:
    pass


class TestSuspendedContextCollection(SpecialsTestCase):

    async def test_pending_return_value_is_visible_to_the_collector(self):
        """
        a coroutine parked in an after thunk while returning a value
        that refers back to the coroutine is a cycle, and is collected
        """

        src_str = """
        (lambda [box]
          (dyn-wind []
            (return box)
            (call park)))
        """
        expr, mod = self.compile_expr(src_str, park=Park)
        fn = await expr()

        box = Box()
        coro = fn(box)
        parked = coro.send(None)
        self.assertIsInstance(parked, Park)

        box.coro = coro
        alive = weakref.ref(box)

        del box, coro, parked, fn, expr, mod
        gc.collect()
        self.assertIsNone(alive())


    async def test_return_value_survives_suspended_after_thunk(self):
        """
        a return through a dyn-wind whose after thunk suspends delivers
        the returned value once the thunk completes
        """

        src_str = """
        (lambda [box]
          (dyn-wind []
            (return box)
            (call park)))
        """
        expr, mod = self.compile_expr(src_str, park=Park)
        fn = await expr()

        box = Box()
        coro = fn(box)
        self.assertIsInstance(coro.send(None), Park)
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertIs(caught.exception.value, box)


    async def test_return_value_survives_nested_suspended_after_thunks(self):
        """
        a return through two winds whose after thunks both suspend parks
        twice and still delivers the returned value
        """

        src_str = """
        (lambda [box]
          (dyn-wind []
            (dyn-wind []
              (return box)
              (call park))
            (call park)))
        """
        expr, mod = self.compile_expr(src_str, park=Park)
        fn = await expr()

        box = Box()
        coro = fn(box)
        self.assertIsInstance(coro.send(None), Park)
        self.assertIsInstance(coro.send(None), Park)
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertIs(caught.exception.value, box)


class TestSuspendedNamespaceAttribution(SpecialsTestCase):

    async def test_object_bound_in_suspended_scopes_survives_collection(self):
        """
        an object bound in a let inside a dyn-wind body, with the
        coroutine parked in that body, is held by the frame's namespace,
        the let's, and the wind's fill of the let's; the collector must
        see each reference once, so a live external reference keeps the
        object alive across a collection
        """

        src_str = """
        (lambda [box]
          (let [[held box]]
            (dyn-wind []
              (begin (call park) held)
              nil)))
        """
        expr, mod = self.compile_expr(src_str, park=Park)
        fn = await expr()

        box = Box()
        box.tag = "still here"
        alive = weakref.ref(box)
        coro = fn(box)
        self.assertIsInstance(coro.send(None), Park)

        gc.collect()
        self.assertIsNotNone(alive())
        self.assertEqual(box.tag, "still here")

        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertIs(caught.exception.value, box)


class TestObligationsAcrossSuspension(SpecialsTestCase):

    async def test_raise_survives_suspended_after_thunk(self):
        """
        an exception unwinding through a dyn-wind whose after thunk
        suspends is parked on the wind and propagates once the thunk
        completes
        """

        src_str = """
        (lambda []
          (dyn-wind []
            (raise (call ValueError "body"))
            (call park)))
        """
        expr, mod = self.compile_expr(src_str, park=Park, ValueError=ValueError)
        fn = await expr()

        coro = fn()
        self.assertIsInstance(coro.send(None), Park)
        with self.assertRaises(ValueError) as caught:
            coro.send(None)
        self.assertEqual(str(caught.exception), "body")


    async def test_raising_after_thunk_voids_the_parked_return(self):
        """
        a return parked on a dyn-wind whose after thunk then raises is
        discarded; the thunk's exception propagates to the handler
        """

        src_str = """
        (lambda []
          (try
            (dyn-wind []
              (return 1)
              (begin (call park) (raise (call ValueError "after"))))
            ([ValueError] "caught")))
        """
        expr, mod = self.compile_expr(src_str, park=Park, ValueError=ValueError)
        fn = await expr()

        coro = fn()
        self.assertIsInstance(coro.send(None), Park)
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertEqual(caught.exception.value, "caught")


# The end.
