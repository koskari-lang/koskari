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
Unit tests for lazy namespace GC tracking: namespaces are GC-allocated
but born untracked, and are revealed to the cyclic collector when a
reference can escape into storage

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import gc
import unittest
import weakref

from koskari.lib import (FormalsSpec, ModuleNamespace, Namespace,
                         NamespaceSpec)

from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestNamespaceReveal(unittest.TestCase):
    """Python-exposed namespaces are tracked"""

    def test_module_namespace_tracked_at_birth(self):
        ns = ModuleNamespace("gctest", {})
        self.assertTrue(gc.is_tracked(ns))

    def test_get_namespace_revealed(self):
        nspec = NamespaceSpec("gctest", ("a",), ())
        ns = nspec.get_namespace(None)
        self.assertTrue(gc.is_tracked(ns))

    def test_constructor_revealed(self):
        ns = Namespace("gctest")
        self.assertTrue(gc.is_tracked(ns))

    def test_copy_revealed(self):
        nspec = NamespaceSpec("gctest", ("a",), ())
        ns = nspec.get_namespace(None)
        self.assertTrue(gc.is_tracked(ns.copy()))

    def test_reveal_walks_parent_chain(self):
        parent_spec = NamespaceSpec("parent", ("p",), ())
        parent = parent_spec.get_namespace(None)
        child_spec = NamespaceSpec("child", ("c",), ())
        child = child_spec.get_namespace(parent)
        self.assertTrue(gc.is_tracked(child))
        self.assertTrue(gc.is_tracked(parent))


class TestLocalsReveal(unittest.TestCase):
    """The LOCALS opcode reveals the reified namespace"""

    def test_locals_result_tracked(self):
        code, ns = build_expr([["LOCALS", 0], ["RETURN", 1]])
        expr = make_callable(code, ns)
        reified = expr().demand()
        self.assertTrue(gc.is_tracked(reified))


class TestNamespaceCycleCollects(unittest.TestCase):
    """
    The motivating case: a function stored into the namespace it closes
    over forms a cycle the collector can now see and reclaim
    """

    def test_function_namespace_cycle_collects(self):
        code, ns = build_expr(
            [["CONST", None], ["RETURN", 1]], local_syms=("selfref",))
        fn = make_callable(code, ns, FormalsSpec((), ()))
        ns.store("selfref", fn)

        code_ref = weakref.ref(code)
        del code, ns, fn
        gc.collect()
        self.assertIsNone(code_ref())


class TestFreelistChurn(unittest.TestCase):
    """Tracked namespaces recycle through the freelist safely"""

    def test_reveal_and_release_churn(self):
        nspec = NamespaceSpec("churn", ("x",), ())
        for _ in range(256):
            ns = nspec.get_namespace(None)
            ns.store("x", object())
            del ns
        gc.collect()
        ns = nspec.get_namespace(None)
        self.assertTrue(gc.is_tracked(ns))


# The end.
