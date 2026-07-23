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
tests.reproducers.test_param_task_isolation

Reproducer: two concurrently-driven tasks sharing one parameter store
corrupted each other's parameterize bindings across awaits (leak,
lost-binding, stale-restore). Under per-context parameter cells
(design/archive/domain-task-isolation.md) each context forks its
cell table at creation, so sibling extents swap cells in disjoint
tables and every task reads its own binding.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


# The design doc's reproducer: siblings interleave mid-extent via sleeps.
SLEEP_SRC = """
define results #{}

define worker [name delay]:
    (parameterize [[p name]]
        (sleep delay)
        (set! (item results name) (parameterq p "unset")))

define main []:
    define t1 (create-task (defer worker "A" 0.01))
    define t2 (create-task (defer worker "B" 0.02))
    (sleep 0.05)
    (parameterq p "root-default")

define outcome (main)
"""


# Deterministic interleave: an Event handshake forces B to bind and read
# while A is suspended mid-extent, and A to resume only afterward.
HANDSHAKE_SRC = """
import-from asyncio Event

define a-bound (Event)
define b-done (Event)
define reads #{}

define worker-a []:
    (parameterize [[q "A"]]
        (set! (item reads "a-first") (parameterq q "unset"))
        (a-bound.set)
        (b-done.wait)
        (set! (item reads "a-second") (parameterq q "unset")))

define worker-b []:
    (a-bound.wait)
    (parameterize [[q "B"]]
        (set! (item reads "b-inside") (parameterq q "unset")))
    (set! (item reads "b-after") (parameterq q "unset"))
    (b-done.set)

define main []:
    define ta (create-task (defer worker-a))
    define tb (create-task (defer worker-b))
    (await ta)
    (await tb)
    (parameterq q "root-default")

define outcome (main)
"""


# Fork semantics without any interleaving: table isolation both
# directions, the shared-cell write-through channel, and extent capture.
# One function, one context: each top-level module form evaluates in its
# own context, so cross-form parameter state does not persist.
FORK_SRC = """
(define run-checks (lambda []
  (define out #{})

  ;; child->parent: a key first created in the child stays in the child
  (set! (item out "child-stored")
        (await (defer (lambda [] (.deref (parameter-ref "k1" "child-value"))))))
  (set! (item out "parent-k1") (parameterq k1 "parent-default"))

  ;; parent->child: a key created after the fork is invisible
  (define k2-task (defer (lambda [] (parameterq k2 "child-default"))))
  (set! (item out "parent-k2") (.deref (parameter-ref "k2" "parent-touch")))
  (set! (item out "child-k2") (await k2-task))

  ;; write-through: a cell shared at fork is a live channel (D7b)
  (parameter-ref "k3" "before-fork")
  (define k3-ref (ref (parameter k3)))
  (await (defer (lambda [] (k3-ref.store "child-wrote"))))
  (set! (item out "parent-k3") (parameterq k3 "unset"))

  ;; snapshot captures the applied extent; survives the extent's exit
  (define captured
    (parameterize [[k4 "extent-value"]]
      (create-task (defer (lambda [] (parameterq k4 "unset"))))))
  (set! (item out "after-extent") (parameterq k4 "outside"))
  (set! (item out "child-k4") (await captured))

  out))

(define fork-results (run-checks))
"""


# A task dropped while suspended inside parameterize cannot disturb the
# parent's view; its extent dies with its own context.
DROP_SRC = """
import-from asyncio Event

define never (Event)

define stuck []:
    (parameterize [[r "stuck-binding"]]
        (never.wait))

define main []:
    define t (create-task (defer stuck))
    (sleep 0.01)
    define seen (parameterq r "parent-default")
    (t.cancel)
    seen

define outcome (main)
"""


class TestParamTaskIsolation(unittest.IsolatedAsyncioTestCase):
    """
    Sibling contexts never observe or destroy each other's parameterize
    bindings
    """

    async def load(self, name, source):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, source)
        return module

    async def test_sleep_interleave_reproducer(self):
        """
        The design-doc reproducer: each task reads its own binding, and
        the parameter resolves to its default after both extents close
        """

        mod = await self.load("param_iso_sleep", SLEEP_SRC)
        ns = vars(mod)
        self.assertEqual(ns["results"], {"A": "A", "B": "B"})
        self.assertEqual(ns["outcome"], "root-default")

    async def test_handshake_interleave_deterministic(self):
        """
        Event handshake pins the schedule: B binds and reads while A is
        parked mid-extent; A still reads its own binding on resume
        """

        mod = await self.load("param_iso_handshake", HANDSHAKE_SRC)
        reads = vars(mod)["reads"]
        self.assertEqual(reads["a-first"], "A")
        self.assertEqual(reads["b-inside"], "B")
        self.assertEqual(reads["b-after"], "unset")
        self.assertEqual(reads["a-second"], "A")
        self.assertEqual(vars(mod)["outcome"], "root-default")

    async def test_fork_semantics(self):
        """
        Table isolation both directions, shared-cell write-through, and
        snapshot capture of applied extents
        """

        mod = await self.load("param_iso_fork", FORK_SRC)
        out = vars(mod)["fork-results"]
        self.assertEqual(out["child-stored"], "child-value")
        self.assertEqual(out["parent-k1"], "parent-default")
        self.assertEqual(out["parent-k2"], "parent-touch")
        self.assertEqual(out["child-k2"], "child-default")
        self.assertEqual(out["parent-k3"], "child-wrote")
        self.assertEqual(out["after-extent"], "outside")
        self.assertEqual(out["child-k4"], "extent-value")

    async def test_drop_mid_extent_harmless(self):
        """
        A task abandoned while suspended inside parameterize leaves the
        parent's parameter view untouched
        """

        mod = await self.load("param_iso_drop", DROP_SRC)
        self.assertEqual(vars(mod)["outcome"], "parent-default")


# The end.
