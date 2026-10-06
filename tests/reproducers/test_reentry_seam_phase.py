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
tests.reproducers.test_reentry_seam_phase

An undelimited continuation re-enters a region that is still live but
has moved on since the capture: a dyn-wind running its after thunk, a
try running its handler, a finally handling a raise, a for-each with a
step in flight. Such a node is not the state the continuation
describes, so the invoke leaves it and lands the template's copy fresh,
with the enter replay that implies.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from tests.compile.specials import SpecialsTestCase


pytestmark = pytest.mark.phase_reproducers


class Box:
    def __init__(self):
        self.k = None
        self.count = 0
        self.passes = 0
        self.log = []


def stash(box, k):
    box.k = k


def saved(box):
    return box.k


def bump(box):
    box.count += 1
    return box.count


def count(box):
    return box.count


def reenter(box):
    """
    True once: the first time it is asked
    """

    if box.passes < 1:
        box.passes += 1
        return True
    return False


def record(box, item):
    box.log.append(item)


class TestReentrySeamPhase(SpecialsTestCase):

    async def evaluate(self, src, box):
        expr, _mod = self.compile_expr(
            src, box=box, stash=stash, saved=saved, bump=bump,
            count=count, reenter=reenter, record=record,
            lt=lambda a, b: a < b, eq=lambda a, b: a == b,
            pair=lambda a, b: (a, b),
            ValueError=ValueError, StopIteration=StopIteration)
        return await expr()


    async def test_after_thunk_reenters_the_wind_body(self):
        box = Box()
        src = """
        (dyn-wind
          (call record box "before")
          (begin
            (call/cc (lambda [k] (call stash box k)))
            (call bump box)
            (call record box (call pair "after-ident" (call count box)))
            (call pair "ret" (call count box)))
          (begin
            (call record box "after")
            (cond [(call reenter box) ((call saved box) None)])))
        """
        self.assertEqual(("ret", 2), await self.evaluate(src, box))
        self.assertEqual(
            ["before", ("after-ident", 1), "after",
             "before", ("after-ident", 2), "after"],
            box.log)


    async def test_handler_retries_the_try_body(self):
        box = Box()
        src = """
        (try
          (begin
            (call/cc (lambda [k] (call stash box k)))
            (call bump box)
            (cond [(call lt (call count box) 3)
                   (raise (call ValueError "fail"))]
                  [else: "ok"]))
          ([e ValueError] ((call saved box) None)))
        """
        self.assertEqual("ok", await self.evaluate(src, box))
        self.assertEqual(3, box.count)


    async def test_finally_retries_the_try_body(self):
        box = Box()
        src = """
        (try
          (begin
            (call/cc (lambda [k] (call stash box k)))
            (call bump box)
            (cond [(call lt (call count box) 2)
                   (raise (call ValueError "fail"))]
                  [else: "ok"]))
          [finally:
            (cond [(call lt (call count box) 2) ((call saved box) None)])])
        """
        self.assertEqual("ok", await self.evaluate(src, box))
        self.assertEqual(2, box.count)


    async def test_generator_reenters_the_consumer_body(self):
        box = Box()
        src = """
        (begin
          (define g (lambda []
            (yield 1)
            ((call saved box) "from-gen")
            (yield 2)))
          (for-each [x (g)]
            (define v (call/cc (lambda [k] (call stash box k) "first")))
            (call record box (call pair x v))
            (cond [(call eq v "from-gen")
                   (raise (call StopIteration "consumer's own"))])))
        """
        # the consumer's own StopIteration escapes the awaited expression
        # body, which PEP 479 reports as a RuntimeError with it as cause
        with self.assertRaises(RuntimeError) as caught:
            await self.evaluate(src, box)
        self.assertIsInstance(caught.exception.__cause__, StopIteration)
        self.assertEqual(("consumer's own",), caught.exception.__cause__.args)
        self.assertEqual([(1, "first"), (1, "from-gen")], box.log)


# The end.
