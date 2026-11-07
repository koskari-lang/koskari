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
Unit tests for a build without read-only code protection: the code
sealing pragma settings, code placement and kio record loading

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.compile.compiler import Compiler, CompilerMode
from koskari.compile.pragma.settings import (
    compiler_pack_module, compiler_protect_code)
from koskari.lib import (
    Arena, Code, CodeAllocator, NamespaceSpec, OpCode, build_flags, values)
from koskari.pragma import ModulePragma
from koskari.serialize.codex import CodeFlags, code_from_record


pytestmark = pytest.mark.phase_compile


SETTINGS = ("compiler/protect-code", "compiler/pack-module")


def instructions():
    """
    The body of a code object that returns a constant
    """

    return [(OpCode.CONST, "payload"), (OpCode.RETURN, 1)]


@unittest.skipUnless(
    build_flags['KOS_NO_PROTECT'],
    "built with read-only code protection")
class TestNoProtect(unittest.TestCase):

    def test_baselines_off(self):
        pragma = ModulePragma()
        for name in SETTINGS:
            self.assertIs(pragma.get(name), False, name)

    def test_setting_functions_refuse_true(self):
        for setting in (compiler_protect_code, compiler_pack_module):
            with self.assertRaises(ValueError):
                setting()
            with self.assertRaises(ValueError):
                setting(True)

    def test_setting_functions_accept_false(self):
        self.assertIs(compiler_protect_code(False), False)
        self.assertIs(compiler_pack_module(False), False)

    def test_rebuild_refuses_true(self):
        for name in SETTINGS:
            with self.assertRaises(ValueError):
                ModulePragma().rebuild((name, values(True)))

    def test_rebuild_refuses_bare_declaration(self):
        for name in SETTINGS:
            with self.assertRaises(ValueError):
                ModulePragma().rebuild((name, values()))

    def test_rebuild_accepts_false(self):
        for name in SETTINGS:
            pragma = ModulePragma().rebuild((name, values(False)))
            self.assertIs(pragma.get(name), False, name)

    def test_compiler_does_not_pack(self):
        self.assertIs(Compiler("noprotect", {}).pack_module, False)

    def test_compiled_function_is_heap(self):
        comp = Compiler("noprotect", {})
        comp.open_allocator()
        frag = comp.fragment("f")
        frag.instruction(OpCode.CONST, "payload")
        frag.instruction(OpCode.RETURN, 1)
        code = comp.complete(frag, mode=CompilerMode.FUNCTION)

        self.assertIsNone(code.arena)
        self.assertEqual(comp.seal(), ())

    def test_code_ignores_protect_and_allocator(self):
        allocator = CodeAllocator()
        code = Code(NamespaceSpec("c", (), ()), instructions(), "c", "<test>",
                    protect=True, allocator=allocator)

        self.assertIs(code.protected, False)
        self.assertIsNone(code.arena)
        self.assertEqual(allocator.seal(), ())

    def test_kio_record_flags_load_as_heap(self):
        allocator = CodeAllocator()
        code = code_from_record(
            NamespaceSpec("c", (), ()), instructions(), "c", "<test>",
            CodeFlags.PROTECT | CodeFlags.ARENA, allocator)

        self.assertIs(code.protected, False)
        self.assertIsNone(code.arena)
        self.assertEqual(allocator.seal(), ())

    def test_no_chunk_unit(self):
        self.assertEqual(Arena.CHUNK, 0)


@unittest.skipIf(
    build_flags['KOS_NO_PROTECT'],
    "built without read-only code protection")
class TestProtectAvailable(unittest.TestCase):

    def test_setting_functions_accept_true(self):
        self.assertIs(compiler_protect_code(), True)
        self.assertIs(compiler_pack_module(), True)

    def test_compiler_packs(self):
        self.assertIs(Compiler("protect", {}).pack_module, True)


# The end.
