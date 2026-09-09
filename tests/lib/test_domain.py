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

import unittest

import pytest

from koskari.lib import Domain, ParamRef
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


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
        Domain.current_domain reports active scoped Domain or None
        """

        self.assertIsNone(Domain.current_domain())
        with Domain() as dom:
            self.assertIs(Domain.current_domain(), dom)
        self.assertIsNone(Domain.current_domain())

    def test_domain_get_contexts_returns_list(self):
        """
        Domain.get_contexts returns a list for the current TLS thread
        """

        with Domain() as dom:
            contexts = dom.get_contexts()
            self.assertIsInstance(contexts, list)

    def test_domain_get_contexts_shape_when_present(self):
        """
        Snapshot entries include baseline keys when contexts are present
        """

        with Domain() as dom:
            contexts = dom.get_contexts()
            for entry in contexts:
                self.assertIn("schema_version", entry)
                self.assertIn("context_id", entry)
                self.assertIn("flags", entry)
                self.assertIn("suspension_event", entry)
                self.assertIn("domain", entry)
                self.assertIn("namespace", entry)
                self.assertIn("dynnodes", entry)

                self.assertIs(entry["domain"], dom)
                self.assertIsInstance(entry["dynnodes"], list)
                for dynnode in entry["dynnodes"]:
                    self.assertIn("node_id", dynnode)
                    self.assertIn("kind", dynnode)
                    self.assertIn("namespace", dynnode)
                    self.assertIn("stack", dynnode)
                    self.assertIn("data", dynnode)


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


    def test_domain_no_domain_outside_with(self):
        """
        After with Domain(): pass, ParamRef with no default raises
        """

        p = ParamRef("x")
        with Domain():
            pass
        with self.assertRaises(RuntimeError):
            p.deref()


# The end.
