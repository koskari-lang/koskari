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
Runtime tests for the IMPORT, IMPORT_FROM, and IMPORT_STAR opcodes.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import sys
import types

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


_ROOT = 0
_LEAF = 1


class _ImportTest(CevalTest):
    """
    Base with helpers for synthetic modules and import evaluation
    """

    def synth_module(self, name, **attrs):
        """
        Install a synthetic module into sys.modules for the duration of
        the test
        """

        mod = types.ModuleType(name)
        for key, val in attrs.items():
            setattr(mod, key, val)
        sys.modules[name] = mod
        self.addCleanup(sys.modules.pop, name, None)

        parent, _, leaf = name.rpartition('.')
        if parent and parent in sys.modules:
            setattr(sys.modules[parent], leaf, mod)

        return mod

    async def run_import(self, instr, local_syms, values=None):
        """
        Evaluate instructions and return the resulting namespace
        """

        code, ns = build_expr(
            instr + [["CONST", None], ["RETURN", 1]],
            local_syms=local_syms, values=values)
        await make_callable(code, ns)()
        return ns


class TestImportOpcode(_ImportTest):

    async def test_import_root_binding(self):
        ns = await self.run_import(
            [["IMPORT", ("os.path", 0, _ROOT, "os")]], ("os",))
        import os
        self.assertIs(ns.get("os"), os)

    async def test_import_leaf_binding(self):
        ns = await self.run_import(
            [["IMPORT", ("os.path", 0, _LEAF, "p")]], ("p",))
        import os.path
        self.assertIs(ns.get("p"), os.path)

    async def test_import_missing_module_raises(self):
        code, ns = build_expr([
            ["IMPORT", ("no_such_module_xyz", 0, _ROOT, "m")],
            ["RETURN", 0],
        ], local_syms=("m",))
        with self.assertRaises(ModuleNotFoundError):
            await make_callable(code, ns)()

    async def test_import_relative_leaf(self):
        self.synth_module("kospkg_i")
        self.synth_module("kospkg_i.sub", marker=42)
        ns = await self.run_import(
            [["IMPORT", ("sub", 1, _LEAF, "sub")]],
            ("sub", "__name__", "__package__"),
            values={"__name__": "kospkg_i.mod", "__package__": "kospkg_i"})
        self.assertIs(ns.get("sub"), sys.modules["kospkg_i.sub"])


class TestImportFromOpcode(_ImportTest):

    async def test_members_bound_with_renames(self):
        self.synth_module("kosmod_f", alpha=1, beta=2)
        ns = await self.run_import(
            [["IMPORT_FROM", ("kosmod_f", 0, ("alpha", "beta"),
                              ("alpha", "b"))]],
            ("alpha", "b"))
        self.assertEqual(ns.get("alpha"), 1)
        self.assertEqual(ns.get("b"), 2)

    async def test_missing_member_raises(self):
        self.synth_module("kosmod_g", alpha=1)
        code, ns = build_expr([
            ["IMPORT_FROM", ("kosmod_g", 0, ("nope",), ("nope",))],
            ["RETURN", 0],
        ], local_syms=("nope",))
        with self.assertRaises((ImportError, AttributeError)):
            await make_callable(code, ns)()

    async def test_submodule_fallback(self):
        # make 'futures' unreachable as an attribute of 'concurrent' so
        # the getattr raises AttributeError and the opcode falls back to
        # importing the submodule (the state a package whose __init__
        # does not import its submodules presents naturally)
        import concurrent
        sys.modules.pop("concurrent.futures", None)
        if hasattr(concurrent, "futures"):
            delattr(concurrent, "futures")

        ns = await self.run_import(
            [["IMPORT_FROM", ("concurrent", 0, ("futures",),
                              ("futures",))]],
            ("futures",))
        self.assertIs(ns.get("futures"), sys.modules["concurrent.futures"])
        self.assertIs(concurrent.futures, sys.modules["concurrent.futures"])


class TestImportStarOpcode(_ImportTest):

    async def test_star_with_all(self):
        self.synth_module(
            "kosmod_s1", __all__=("alpha",), alpha=1, beta=2, _hid=3)
        ns = await self.run_import(
            [["IMPORT_STAR", ("kosmod_s1", 0, 0, 0)]],
            ("alpha", "beta", "_hid"))
        self.assertEqual(ns.get("alpha"), 1)
        self.assertIsNone(ns.get("beta"))
        self.assertIsNone(ns.get("_hid"))

    async def test_star_without_all(self):
        self.synth_module("kosmod_s2", alpha=1, beta=2, _hid=3)
        ns = await self.run_import(
            [["IMPORT_STAR", ("kosmod_s2", 0, 0, 0)]],
            ("alpha", "beta", "_hid"))
        self.assertEqual(ns.get("alpha"), 1)
        self.assertEqual(ns.get("beta"), 2)
        self.assertIsNone(ns.get("_hid"))

    async def test_star_overwrites_existing(self):
        self.synth_module("kosmod_s3", alpha=99)
        ns = await self.run_import(
            [["IMPORT_STAR", ("kosmod_s3", 0, 0, 0)]],
            ("alpha",), values={"alpha": 1})
        self.assertEqual(ns.get("alpha"), 99)

    async def test_star_non_string_all_entry_raises(self):
        self.synth_module("kosmod_s4", __all__=("alpha", 42), alpha=1)
        code, ns = build_expr([
            ["IMPORT_STAR", ("kosmod_s4", 0, 0, 0)],
            ["RETURN", 0],
        ], local_syms=("alpha",))
        with self.assertRaises(TypeError):
            await make_callable(code, ns)()

    async def test_star_missing_all_name_raises(self):
        self.synth_module("kosmod_s5", __all__=("ghost",))
        code, ns = build_expr([
            ["IMPORT_STAR", ("kosmod_s5", 0, 0, 0)],
            ["RETURN", 0],
        ], local_syms=("ghost",))
        with self.assertRaises(AttributeError):
            await make_callable(code, ns)()


# The end.
