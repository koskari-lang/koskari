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
Tests for .kio serialization: round-trip, validation, and importer parity.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import io
import struct
import unittest

import pytest

from koskari.lib import (Code, FormalsSpec, FunctionSpec, NamespaceSpec,
                         OpCode, bracket_nil, demand_call, nil)
from koskari.serialize import compile_ki_source_to_kio, dump_kio, load_kio

from .. import make_callable
from ..lib.opcode import build_expr

pytestmark = pytest.mark.phase_compile


class TestKioRoundTrip(unittest.TestCase):
    """
    dump_kio / load_kio identity for Code objects in a KioFile
    """

    def test_const_expression_roundtrip(self):
        code, ns = build_expr([
            ['CONST', 42],
            ['RETURN', 1],
        ])
        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        kio = load_kio(buf)
        self.assertEqual(kio.header.section_dir_count, 3)
        self.assertEqual(len(kio.codes), 1)
        expr = make_callable(kio.codes[0], ns)
        self.assertEqual(demand_call(expr), 42)


    def _const_roundtrip(self, value):
        code, _ns = build_expr([
            ['CONST', value],
            ['RETURN', 1],
        ])
        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        kio = load_kio(buf)
        for op, arg in kio.codes[0].soften():
            if op == OpCode.KOS_OP_CONST:
                return arg
        self.fail("no CONST in roundtripped code")

    def test_nil_const_roundtrip(self):
        """nil survives the const-pool round-trip as the nil singleton"""
        self.assertIs(self._const_roundtrip(nil), nil)

    def test_bracket_nil_const_roundtrip(self):
        """bracket-nil round-trips as bracket-nil, not collapsed to nil"""
        self.assertIs(self._const_roundtrip(bracket_nil), bracket_nil)


    def test_function_spec_const_roundtrip(self):
        inner_code, ns_inner = build_expr([
            ['CONST', 7],
            ['RETURN', 1],
        ], name='inner')
        fspec = FunctionSpec('inner', FormalsSpec(), inner_code)
        code, _ = build_expr([
            ['CONST', fspec],
            ['RETURN', 1],
        ], name='outer')
        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        kio = load_kio(buf)
        self.assertEqual(len(kio.codes), 1)
        loaded = None
        for op, arg in kio.codes[0].soften():
            if op == OpCode.KOS_OP_CONST:
                loaded = arg
                break
        self.assertIsInstance(loaded, FunctionSpec)
        inner_expr = make_callable(loaded.code, ns_inner)
        self.assertEqual(demand_call(inner_expr), 7)


class TestSourceFilenameIndirection(unittest.TestCase):
    """
    Embedded body filename can differ from the compile-time path
    """

    def test_compile_ki_embeds_source_filename(self):
        logical = 'pkg/module.ki'
        buf = io.BytesIO()
        compile_ki_source_to_kio(
            '(define _sf_indir 0)\n',
            buf,
            source_filename=logical)
        buf.seek(0)
        kio = load_kio(buf)
        self.assertEqual(len(kio.codes), 1)
        self.assertEqual(kio.codes[0].filename, logical)


class TestSoftenRebuild(unittest.TestCase):
    """
    Native soften output round-trips through Code(...)
    """

    def test_soften_rebuild_runs(self):
        code, ns = build_expr([
            ['CONST', 42],
            ['RETURN', 1],
        ])
        instr = code.soften()
        code2 = Code(code.namespace_spec, instr, code.name, code.filename)
        self.assertEqual(demand_call(make_callable(code2, ns)), 42)


class TestFastValidation(unittest.TestCase):
    """
    FAST slot bounds enforced on re-harden
    """

    def test_fast_index_rejected(self):
        nspec = NamespaceSpec(None, ('a',), ())
        instr = [(OpCode.KOS_OP_FAST, 1)]
        with self.assertRaises(ValueError) as ctx:
            Code(nspec, instr, 'x', 'y.ki')
        self.assertIn('FAST', str(ctx.exception))


class TestKioMalformed(unittest.TestCase):
    """
    Reject corrupt inputs
    """

    def test_bad_magic(self):
        data = b'XXXX' + b'\x00' * 40
        with self.assertRaises(ValueError) as ctx:
            load_kio(io.BytesIO(data))
        self.assertIn('magic', str(ctx.exception).lower())


    def test_rejects_stale_bytecode_abi(self):
        code, _ = build_expr([
            ['CONST', 42],
            ['RETURN', 1],
        ])
        buf = io.BytesIO()
        dump_kio(buf, [code])
        data = bytearray(buf.getvalue())
        struct.pack_into('<I', data, 8, 999)
        with self.assertRaises(ValueError) as ctx:
            load_kio(io.BytesIO(data))
        self.assertIn('abi', str(ctx.exception).lower())


# The end.
