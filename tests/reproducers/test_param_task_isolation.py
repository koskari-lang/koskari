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
lost-binding, stale-restore). Under per-context shadow tables
(design/archive/parameter-resolution.md) each context forks its table at
creation, so sibling extents swap cells in disjoint tables and every
task reads its own binding.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


# The design doc's reproducer: siblings interleave mid-extent via sleeps.
SLEEP_SRC = """
import-from asyncio sleep

var results #{}

def worker [name delay]:
    (parameterize [[p name]]
        (sleep delay)
        (set! (item results name) (parameterq p "unset")))

def main []:
    var t1 (create-task (defer worker "A" 0.01))
    var t2 (create-task (defer worker "B" 0.02))
    (sleep 0.05)
    (parameterq p "root-default")

var outcome (main)
"""


# Deterministic interleave: an Event handshake forces B to bind and read
# while A is suspended mid-extent, and A to resume only afterward.
HANDSHAKE_SRC = """
import-from asyncio Event sleep

var a-bound (Event)
var b-done (Event)
var reads #{}

def worker-a []:
    (parameterize [[q "A"]]
        (set! (item reads "a-first") (parameterq q "unset"))
        (a-bound.set)
        (b-done.wait)
        (set! (item reads "a-second") (parameterq q "unset")))

def worker-b []:
    (a-bound.wait)
    (parameterize [[q "B"]]
        (set! (item reads "b-inside") (parameterq q "unset")))
    (set! (item reads "b-after") (parameterq q "unset"))
    (b-done.set)

def main []:
    var ta (create-task (defer worker-a))
    var tb (create-task (defer worker-b))
    (await ta)
    (await tb)
    (parameterq q "root-default")

var outcome (main)
"""


# Fork semantics without any interleaving. A store with no enclosing
# extent lands in the domain's root scope, which parent and children
# share, so it crosses the fork in both directions. A parameterize
# extent lands in the forking context's own table, so the fork captures
# the extents applied at creation and nothing later.
FORK_SRC = """
(def run-checks []
  (var out #{})

  ;; parent->child through the root scope
  (var k1-ref (ref (parameter k1)))
  (k1-ref.store "parent-wrote")
  (set! (item out "child-sees-parent")
        (await (defer (lambda [] (parameterq k1 "unset")))))

  ;; child->parent through the root scope
  (await (defer (lambda [] (.store (ref (parameter k2)) "child-wrote"))))
  (set! (item out "parent-sees-child") (parameterq k2 "unset"))

  ;; the fork captures the extent applied at creation, and it survives
  ;; the extent's exit
  (var captured
    (parameterize [[k3 "extent-value"]]
      (create-task (defer (lambda [] (parameterq k3 "unset"))))))
  (set! (item out "after-extent") (parameterq k3 "outside"))
  (set! (item out "child-k3") (await captured))

  ;; a defer evaluates at its await, under the awaiter's extent: an
  ;; extent entered between the defer and the await is visible to it,
  ;; as a ContextVar set before a direct await is
  (var late (defer (lambda [] (parameterq k4 "child-default"))))
  (set! (item out "child-k4")
        (parameterize [[k4 "at-await"]]
          (await late)))

  out)

(var fork-results (run-checks))
"""


# A task dropped while suspended inside parameterize cannot disturb the
# parent's view; its extent dies with its own context.
DROP_SRC = """
import-from asyncio Event sleep

var never (Event)

def stuck []:
    (parameterize [[r "stuck-binding"]]
        (never.wait))

def main []:
    var t (create-task (defer stuck))
    (sleep 0.01)
    var seen (parameterq r "parent-default")
    (t.cancel)
    seen

var outcome (main)
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
        Event handshake fixes the schedule: B binds and reads while A is
        suspended mid-extent; A still reads its own binding on resume
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
        Root-scope stores cross the fork both directions; a task captures
        the extents applied when it was made, and a defer awaited
        directly runs under the awaiter's extent
        """

        mod = await self.load("param_iso_fork", FORK_SRC)
        out = vars(mod)["fork-results"]
        self.assertEqual(out["child-sees-parent"], "parent-wrote")
        self.assertEqual(out["parent-sees-child"], "child-wrote")
        self.assertEqual(out["after-extent"], "outside")
        self.assertEqual(out["child-k3"], "extent-value")
        self.assertEqual(out["child-k4"], "at-await")

    async def test_drop_mid_extent_harmless(self):
        """
        A task abandoned while suspended inside parameterize leaves the
        parent's parameter view untouched
        """

        mod = await self.load("param_iso_drop", DROP_SRC)
        self.assertEqual(vars(mod)["outcome"], "parent-default")


# The end.
