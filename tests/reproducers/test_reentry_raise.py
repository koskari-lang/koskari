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
tests.reproducers.test_reentry_raise

A continuation re-enters the extents it captured outermost first, and
each dyn-wind before thunk or dyn-with __enter__ runs with only the
extents outside it in place, as dynamic-wind runs a before thunk in
the extent of the dynamic-wind itself. A raise from it is raised
there: the body it guards was never entered, so a try inside that
body does not see it and its after thunk or __exit__ does not run;
the extents already re-entered unwind as for any exception, and the
raise reaches the call of the continuation.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(define-parameter outer-p 0)
(define-parameter inner-p 0)

def failing [log n label]:
    (log.append label)
    (n.append 1)
    (when (> (len n) 1) (raise (ValueError "x")))

class FailingMgr []:
    def __init__ [self log]:
        (set! self.log log)
        (set! self.count 0)
    def __enter__ [self]:
        (self.log.append "enter")
        (set! self.count (+ self.count 1))
        (when (> self.count 1) (raise (ValueError "x")))
        self
    def __exit__ [self *: args]:
        (self.log.append "exit")
        False

def ruled [log]:
    var n #[]
    var r (reset
        (dyn-wind (failing log n "before")
                  (try (+ 1 (shift k (begin (log.append "handler")
                                            (try (k 10)
                                                 [[e ValueError] "caught-at-k"]))))
                       [[e ValueError] "caught-inside"])
                  (log.append "after")))
    #[r log]

def no_try_at_k [log]:
    var n #[]
    (reset
        (dyn-wind (failing log n "before")
                  (try (+ 1 (shift k (begin (log.append "handler") (k 10))))
                       [[e ValueError] "caught-inside"])
                  (log.append "after")))

def nested_winds [log]:
    var n #[]
    var r (reset
        (dyn-wind (log.append "b1")
                  (dyn-wind (failing log n "b2")
                            (try (shift k (begin (log.append "handler")
                                                 (try (k 1)
                                                      [[e ValueError] "caught-at-k"])))
                                 [[e ValueError] "caught-inside"])
                            (log.append "a2"))
                  (log.append "a1")))
    #[r log]

def dyn_with_enter [log]:
    var r (reset
        (dyn-with [c (FailingMgr log)]
          (try (shift k (begin (log.append "handler")
                               (try (k 1) [[e ValueError] "caught-at-k"])))
               [[e ValueError] "caught-inside"])))
    #[r log]

def finally_outside [log]:
    var n #[]
    var r (reset
        (try (dyn-wind (failing log n "before")
                       (try (shift k (begin (log.append "handler")
                                            (try (k 10)
                                                 [[e ValueError] "caught-at-k"])))
                            [[e ValueError] "caught-inside"])
                       nil)
             [finally: (log.append "finally")]))
    #[r log]

def param_view [log]:
    var r (reset
        (parameterize [[outer-p 1]]
          (dyn-wind (log.append #[outer-p inner-p])
                    (parameterize [[inner-p 2]] (shift k (k 5)))
                    nil)))
    #[r log]
"""


class TestReentryRaise(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("reentry_raise")
        vars(module)["__file__"] = "<reentry_raise>"
        await load_module_source_async(module, SRC)
        return vars(module)


    async def test_raise_from_before_reaches_the_call_site(self):
        mod = await self.load()
        self.assertEqual(
            ["caught-at-k", ["before", "after", "handler", "before"]],
            await mod["ruled"]([]))


    async def test_raise_from_before_leaves_the_reset(self):
        mod = await self.load()
        log = []
        with self.assertRaises(ValueError):
            await mod["no_try_at_k"](log)
        self.assertEqual(["before", "after", "handler", "before"], log)


    async def test_entered_outer_wind_unwinds_inner_does_not(self):
        mod = await self.load()
        self.assertEqual(
            ["caught-at-k",
             ["b1", "b2", "a2", "a1", "handler", "b1", "b2", "a1"]],
            await mod["nested_winds"]([]))


    async def test_raise_from_dyn_with_enter_skips_its_exit(self):
        mod = await self.load()
        self.assertEqual(
            ["caught-at-k", ["enter", "exit", "handler", "enter"]],
            await mod["dyn_with_enter"]([]))


    async def test_entered_finally_runs_as_the_raise_unwinds(self):
        mod = await self.load()
        self.assertEqual(
            ["caught-at-k", ["before", "handler", "before", "finally"]],
            await mod["finally_outside"]([]))


    async def test_before_sees_only_the_outer_parameterize(self):
        mod = await self.load()
        self.assertEqual(
            [5, [[1, 0], [1, 0]]], await mod["param_view"]([]))


# The end.
