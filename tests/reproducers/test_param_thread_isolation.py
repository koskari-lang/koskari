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
tests.reproducers.test_param_thread_isolation

Threading verifiers for parameter resolution and domain ambience.
Resolution inside the evaluator is context-resident (es -> ctx ->
shadow table, then the domain root scope) and never consults thread
state, so a context created on one thread and driven on another keeps
its captured view; parameterize extents swap cells in per-context
tables, so extents live on different OS threads simultaneously cannot
corrupt each other; the root scope follows the domain rather than the
thread, so a store with no extent crosses threads; and the ambient TLS
state (context stack, python domain scope) is per-thread, so one
thread's ambience is invisible to another until a Domain object is
entered there explicitly.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio
import threading
import unittest

import koskari.asyncio
from koskari.lib import Domain, ParamRef
from koskari.module import load_module_source_async, new_module

import pytest

pytestmark = pytest.mark.phase_reproducers


# A defer created inside a parameterize extent on the loading thread;
# driven later on a different OS thread, it reads its captured binding.
CAPTURE_SRC = """
(define probe
  (parameterize [[tp "captured"]]
    (defer (lambda [] (parameterq tp "unset")))))
"""


# Two defers forked from one context, so both resolve against the same
# domain root scope. Each worker parameterizes `shared` with its own
# name; barrier B1 forces both extents to be live at the same moment on
# different threads. After the extents exit, T1 stores with no extent in
# the way -- which lands in the root scope -- and B2 orders that store
# before T2's read. B1, B2, and RESULTS are pre-seeded into the module
# namespace.
SHARED_SRC = """
(define worker (lambda [name]
  (parameterize [[shared name]]
    (B1.wait 10)
    (set! (item RESULTS name) (parameterq shared "unset")))
  (when (== name "T1")
    (let [[cell (ref (parameter shared))]]
      (cell.store "cross")))
  (B2.wait 10)
  (when (== name "T2")
    (set! (item RESULTS "after") (parameterq shared "unset")))))

(define probes #[(defer worker "T1") (defer worker "T2")])
"""


def drive_on_thread(target, errors):
    """
    Drive one koskari awaitable to completion on a private event loop;
    exceptions are collected rather than lost to the thread boundary.
    """

    def run():
        loop = asyncio.new_event_loop()
        try:
            koskari.asyncio.run_until_complete(loop, target)
        except BaseException as exc:
            errors.append(exc)
        finally:
            loop.close()

    return threading.Thread(target=run)


class TestParamThreadIsolation(unittest.IsolatedAsyncioTestCase):
    """
    Parameter capture, extent isolation, and domain ambience across OS
    threads
    """

    async def load(self, name, source, seed=None):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        if seed:
            for key, value in seed.items():
                vars(module)[key] = value
        await load_module_source_async(module, source)
        return module

    async def test_capture_crosses_threads(self):
        """
        A defer created inside a parameterize extent on this thread reads
        its captured binding when driven on another thread
        """

        mod = await self.load("param_thread_capture", CAPTURE_SRC)
        probe = vars(mod)["probe"]

        errors = []
        results = []

        def run():
            loop = asyncio.new_event_loop()
            try:
                results.append(
                    koskari.asyncio.run_until_complete(loop, probe))
            except BaseException as exc:
                errors.append(exc)
            finally:
                loop.close()

        t = threading.Thread(target=run)
        t.start()
        t.join(30)
        self.assertFalse(t.is_alive(), "driver thread hung")
        self.assertEqual(errors, [])
        self.assertEqual(results, ["captured"])

    async def test_concurrent_extents_on_two_threads(self):
        """
        Two threads inside parameterize extents of the same name at the
        same moment each read their own binding, and a store made with no
        extent in the way reaches the shared root scope and is visible
        across threads afterward
        """

        seed = {
            "B1": threading.Barrier(2),
            "B2": threading.Barrier(2),
            "RESULTS": {},
        }
        mod = await self.load("param_thread_shared", SHARED_SRC, seed=seed)
        t1_probe, t2_probe = vars(mod)["probes"]

        errors = []
        t1 = drive_on_thread(t1_probe, errors)
        t2 = drive_on_thread(t2_probe, errors)
        t1.start()
        t2.start()
        t1.join(30)
        t2.join(30)
        self.assertFalse(t1.is_alive() or t2.is_alive(), "worker hung")
        self.assertEqual(errors, [])

        results = seed["RESULTS"]
        # simultaneous extents, private tables: each read its own value
        self.assertEqual(results["T1"], "T1")
        self.assertEqual(results["T2"], "T2")
        # the fork-shared root cell is a live channel across threads
        self.assertEqual(results["after"], "cross")

    def test_thread_has_no_foreign_ambient(self):
        """
        A second thread sees none of this thread's ambient state; entering
        the same Domain object there is the explicit sharing gesture
        """

        seen = []

        def on_thread_b(dom):
            try:
                ParamRef("cross_thread").deref()
                seen.append("no-raise")
            except RuntimeError:
                seen.append("isolated")
            with dom:
                seen.append(ParamRef("cross_thread").deref())

        with Domain() as dom:
            ParamRef("cross_thread").store("main-value")
            t = threading.Thread(target=on_thread_b, args=(dom,))
            t.start()
            t.join(30)
            self.assertFalse(t.is_alive(), "probe thread hung")

        self.assertEqual(seen, ["isolated", "main-value"])


# The end.
