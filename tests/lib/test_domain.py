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
Unit tests for Domain context manager

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import threading
import unittest

import pytest

from koskari.lib import Context, Domain, ParamRef, demand_call, parameter
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class TestDomainExitArity(unittest.IsolatedAsyncioTestCase):
    """
    __exit__ takes the three exception arguments; a call with another
    count is refused rather than popping the scope with the error pending
    """

    async def test_exit_with_wrong_arity_is_refused(self):
        dom = Domain()
        dom.__enter__()
        with self.assertRaises(TypeError):
            dom.__exit__()
        dom.__exit__(None, None, None)


class TestDomainContextManager(unittest.IsolatedAsyncioTestCase):
    """
    Domain as context manager (with Domain():)
    """

    def test_domain_context_manager_enter_exit(self):
        """
        with Domain(): pass runs without error
        """

        with Domain():
            pass

    def test_domain_current_domain_classmethod(self):
        """
        Domain.current_domain reports the active scoped Domain, and the
        thread's default Domain outside any with
        """

        default = Domain.current_domain()
        self.assertIsInstance(default, Domain)
        self.assertIs(Domain.current_domain(), default)
        with Domain() as dom:
            self.assertIs(Domain.current_domain(), dom)
            self.assertIsNot(dom, default)
        self.assertIs(Domain.current_domain(), default)

    def test_domain_get_contexts_returns_list(self):
        """
        Domain.get_contexts returns a list for the current TLS thread
        """

        with Domain() as dom:
            contexts = dom.get_contexts()
            self.assertIsInstance(contexts, list)

    def test_domain_get_contexts_no_running_context(self):
        """
        With nothing running under the Domain, get_contexts is empty
        """

        with Domain() as dom:
            self.assertEqual(dom.get_contexts(), [])

    def test_domain_get_contexts_reports_running_context(self):
        """
        Called from inside a drive, get_contexts holds the Context
        objects running under the Domain, innermost first, and each
        Context pictures itself with snapshot()
        """

        seen = {}

        def probe():
            dom = Domain.current_domain()
            seen["domain"] = dom
            seen["contexts"] = dom.get_contexts()
            seen["namespace"] = seen["contexts"][0].namespace
            seen["snapshot"] = seen["contexts"][0].snapshot()
            return 7

        code, ns = build_expr([
            ["VAR", "probe"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"probe": probe})
        self.assertEqual(demand_call(make_callable(code, ns)), 7)

        contexts = seen["contexts"]
        self.assertGreaterEqual(len(contexts), 1)
        ctx = contexts[0]
        self.assertIsInstance(ctx, Context)
        self.assertIs(ctx.domain, seen["domain"])
        self.assertIs(seen["namespace"], ns)

        entry = seen["snapshot"]
        for key in ("schema_version", "context_id", "flags",
                    "suspension_event", "domain", "namespace", "dynnodes"):
            self.assertIn(key, entry)
        self.assertEqual(entry["context_id"], id(ctx))
        self.assertIs(entry["domain"], seen["domain"])
        self.assertIs(entry["namespace"], ns)
        self.assertIsInstance(entry["dynnodes"], list)
        self.assertGreaterEqual(len(entry["dynnodes"]), 1)
        for dynnode in entry["dynnodes"]:
            self.assertIn("node_id", dynnode)
            self.assertIn("kind", dynnode)
            self.assertIn("namespace", dynnode)
            self.assertIn("stack", dynnode)
            self.assertIn("data", dynnode)

        # the drive is over: the context is no longer running
        self.assertFalse(ctx.running)

    def test_context_is_not_constructible(self):
        with self.assertRaises(TypeError):
            Context()


    def test_domain_paramref_store_deref_from_python(self):
        """
        ParamRef store and deref work from Python inside with Domain()
        """

        p = ParamRef("x", None)
        with Domain():
            p.store(1)
            self.assertEqual(p.deref(), 1)


    async def test_domain_paramref_after_koskari_return(self):
        """
        ParamRef deref works after koskari returns while inside with Domain()
        """

        p = ParamRef("z", None)
        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        with Domain():
            p.store(1)
            result = await make_callable(code, ns)()
            self.assertEqual(p.deref(), 1)
        self.assertEqual(result, 42)


    async def test_domain_root_scope_is_shared_channel(self):
        """
        The Domain's root scope is reached by resolution from the koskari
        context, so a host pre-define is read inside and a koskari store
        is visible to Python after the return
        """

        p = ParamRef("chan", None)

        def read_then_store():
            seen = p.deref()
            p.store("from-koskari")
            return seen

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": read_then_store})
        with Domain():
            p.store("from-host")
            result = await make_callable(code, ns)()
            self.assertEqual(p.deref(), "from-koskari")
        self.assertEqual(result, "from-host")


    async def test_domain_koskari_store_lands_in_root_scope(self):
        """
        A store made inside koskari with no enclosing parameterize extent
        lands in the Domain's root scope, so Python reads it after the
        return without any pre-define.
        """

        p = ParamRef("fresh_key")

        def store_fresh():
            p.store("from-koskari")
            return p.deref()

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store_fresh})
        with Domain() as domain:
            result = await make_callable(code, ns)()
            self.assertEqual(p.deref(), "from-koskari")
            self.assertEqual(
                domain.param_scopes()["root"]["fresh_key"].deref(),
                "from-koskari")
        self.assertEqual(result, "from-koskari")


    async def test_domain_extent_binding_stays_out_of_root_scope(self):
        """
        A parameterize extent inside koskari shadows in the context's own
        table: the Domain's root scope never sees the binding, and the
        name is unbound again after the return.
        """

        p = ParamRef("extent_key")

        code, ns = build_expr([
            ["CONST", "inside-only"],
            ["PUSH_PARAM", ("extent_key",)],
            ["PARAM", ("extent_key", 0, 0, 0)],
            ["POP_PARAM", 0],
            ["RETURN", 1],
        ])
        with Domain() as domain:
            result = await make_callable(code, ns)()
            self.assertNotIn("extent_key", domain.param_scopes()["root"])
            self.assertTrue(p.empty)
        self.assertEqual(result, "inside-only")


    def test_domain_nested(self):
        """
        Nested with Domain(): scopes; inner exits first, outer domain current again
        """

        p = ParamRef("x", None)
        with Domain():
            p.store(1)
            with Domain():
                p.store(2)
                self.assertEqual(p.deref(), 2)
            self.assertEqual(p.deref(), 1)


    def test_domain_is_gc_tracked_and_cycle_collectable(self):
        """
        Domain participates in GC: instances are tracked, and a reference
        cycle through a stored parameter value is collected
        """

        import gc
        import weakref

        class Marker:
            pass

        dom = Domain()
        self.assertTrue(gc.is_tracked(dom))

        # cycle: domain -> base cell -> list -> domain; the marker rides
        # in the cycle, so it dies only if the cycle is collected
        marker = Marker()
        marker_ref = weakref.ref(marker)
        with dom:
            ParamRef("loop", None).store([dom, marker])
        del dom, marker
        gc.collect()
        self.assertIsNone(marker_ref())


    def test_domain_thread_default_outside_with(self):
        """
        After with Domain(): pass, a ParamRef with no default and no
        store in the thread default raises NameError; a store followed
        by a deref reads the value back
        """

        p = ParamRef("domain_outside_with_key")
        with Domain():
            pass
        with self.assertRaises(NameError):
            p.deref()
        self.addCleanup(p.clear)
        p.store(3)
        self.assertEqual(p.deref(), 3)


class TestThreadDefaultDomain(unittest.TestCase):
    """
    Outside any with Domain(): each thread has one default Domain whose
    root scope persists across calls on that thread and is never seen
    from another thread
    """

    def stored(self, name, value):
        ref = ParamRef(name)
        self.addCleanup(ref.clear)
        ref.store(value)
        return ref


    def test_root_store_outside_with_is_read_later(self):
        """
        A root store made outside any with Domain() is visible to a
        later read on the same thread, through a fresh ParamRef and the
        parameter builtin
        """

        self.stored("tdd_later_key", "kept")
        self.assertEqual(ParamRef("tdd_later_key").deref(), "kept")
        self.assertEqual(parameter("tdd_later_key"), "kept")
        self.assertIn("tdd_later_key",
                      Domain.current_domain().param_scopes()["root"])


    def test_koskari_store_lands_in_thread_default(self):
        """
        A store made from inside a boundary call with no with Domain()
        open lands in the thread default's root scope, so Python reads
        it after the call returns
        """

        p = ParamRef("tdd_koskari_key")
        self.addCleanup(p.clear)

        def store():
            p.store("from-koskari")
            return Domain.current_domain()

        code, ns = build_expr([
            ["VAR", "f"],
            ["CALL", 0],
            ["RETURN", 1],
        ], values={"f": store})
        seen = demand_call(make_callable(code, ns))
        self.assertIs(seen, Domain.current_domain())
        self.assertEqual(p.deref(), "from-koskari")


    def test_second_thread_has_its_own_default(self):
        """
        Another thread sees none of this thread's root stores, and has
        a default Domain of its own, the same object on each call there
        """

        self.stored("tdd_thread_key", "main")
        main_default = Domain.current_domain()
        seen = {}

        def worker():
            ref = ParamRef("tdd_thread_key")
            seen["empty"] = ref.empty
            seen["default"] = Domain.current_domain()
            seen["again"] = Domain.current_domain()
            ref.store("worker")
            seen["value"] = ref.deref()

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()

        self.assertTrue(seen["empty"])
        self.assertIsInstance(seen["default"], Domain)
        self.assertIsNot(seen["default"], main_default)
        self.assertIs(seen["again"], seen["default"])
        self.assertEqual(seen["value"], "worker")
        self.assertEqual(ParamRef("tdd_thread_key").deref(), "main")


    def test_explicit_domain_wins_then_default_returns(self):
        """
        Inside with Domain() the explicit Domain answers and the thread
        default's stores are not seen; after the block the thread
        default answers again with its earlier stores
        """

        p = self.stored("tdd_scope_key", "outer")
        default = Domain.current_domain()
        with Domain() as dom:
            self.assertIs(Domain.current_domain(), dom)
            self.assertTrue(p.empty)
            p.store("inner")
            self.assertEqual(p.deref(), "inner")
        self.assertIs(Domain.current_domain(), default)
        self.assertEqual(p.deref(), "outer")


# The end.
