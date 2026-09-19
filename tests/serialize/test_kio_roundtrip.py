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

from koskari.lib import (
    Code, FormalsSpec, FunctionSpec, NamespaceSpec, OpCode, OpStyle,
    bracket_nil, demand_call, nil, symbol)
from koskari.serialize import (
    KioFile, KioMeta, compile_ki_source_to_kio, dump_kio, load_kio)
from koskari.serialize.codex import (
    KIO_MAGIC, KioHeader, PoolBuilder, SectionTag)

from .. import make_callable
from ..lib.opcode import build_code, build_expr


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
            if op == OpCode.CONST:
                return arg
        self.fail("no CONST in roundtripped code")

    def test_code_ref_shaped_tuple_is_a_tuple(self):
        """
        A constant tuple whose head happens to be the string CODE_REF is
        an ordinary tuple through the .kio, not a code reference
        """

        self.assertEqual(self._const_roundtrip(('CODE_REF', 3)), ('CODE_REF', 3))


    def test_nil_const_roundtrip(self):
        """nil survives the const-pool round-trip as the nil singleton"""
        self.assertIs(self._const_roundtrip(nil), nil)

    def test_bracket_nil_const_roundtrip(self):
        """bracket-nil round-trips as bracket-nil, not collapsed to nil"""
        self.assertIs(self._const_roundtrip(bracket_nil), bracket_nil)

    def test_int_const_roundtrip_at_byte_edges(self):
        """
        Every int survives, including positives whose magnitude fills the
        top byte; those used to overflow the packer
        """
        for value in (0, 1, -1, 127, 128, 255, 256, -128, -129,
                      32767, 32768, 65535, 65536, -32768, -32769,
                      2 ** 31 - 1, 2 ** 31, 2 ** 63, -2 ** 63, 2 ** 64,
                      2 ** 200, -2 ** 200):
            got = self._const_roundtrip(value)
            self.assertEqual(got, value)
            self.assertIs(type(got), int)


    def test_ellipsis_const_roundtrip(self):
        """Ellipsis survives the const-pool round-trip as the singleton"""
        self.assertIs(self._const_roundtrip(...), ...)

    def test_frozenset_const_roundtrip(self):
        """a frozenset of constants round-trips by value, as a frozenset"""
        value = frozenset((1, "two", 3.0, None))
        loaded = self._const_roundtrip(value)
        self.assertIsInstance(loaded, frozenset)
        self.assertEqual(loaded, value)

    def test_slice_const_roundtrip(self):
        """slices round-trip by value, None bounds included"""
        for value in (slice(1, 10, 2), slice(None, 5, None), slice(None)):
            loaded = self._const_roundtrip(value)
            self.assertIsInstance(loaded, slice)
            self.assertEqual(loaded, value)

    def test_nested_frozenset_slice_roundtrip(self):
        """
        containers nest: a tuple holding a frozenset, a tuple holding a
        slice, and Ellipsis. The slice cannot sit inside the frozenset
        because a slice is unhashable before Python 3.12
        """
        value = (frozenset((1, 2)), (slice(1, 2),), ...)
        loaded = self._const_roundtrip(value)
        self.assertEqual(loaded, value)

    def test_code_protect_and_cache_roundtrip(self):
        """a body's protect and cache settings are stored and restored"""
        rows = [['CONST', 1], ['RETURN', 1]]
        nspec = NamespaceSpec(None, (), ())
        instr = build_code(rows).soften()
        plain = Code(nspec, instr, 'plain', protect=False, cache=False)
        sealed = Code(nspec, instr, 'sealed', protect=True, cache=True)
        self.assertFalse(plain.protected)
        self.assertFalse(plain.caches)
        self.assertTrue(sealed.protected)
        self.assertTrue(sealed.caches)
        buf = io.BytesIO()
        dump_kio(buf, [plain, sealed])
        buf.seek(0)
        kio = load_kio(buf)
        loaded_plain, loaded_sealed = kio.codes
        self.assertFalse(loaded_plain.protected)
        self.assertFalse(loaded_plain.caches)
        self.assertTrue(loaded_sealed.protected)
        self.assertTrue(loaded_sealed.caches)

    def test_function_spec_flags_roundtrip(self):
        """a nested function body keeps its protect and cache settings"""
        nspec = NamespaceSpec(None, (), ())
        instr = build_code([['CONST', 7], ['RETURN', 1]]).soften()
        inner = Code(nspec, instr, 'inner', protect=False, cache=False)
        fspec = FunctionSpec('inner', FormalsSpec(), inner)
        code, _ = build_expr([
            ['CONST', fspec],
            ['RETURN', 1],
        ], name='outer')
        loaded = self._const_roundtrip_code(code)
        self.assertIsInstance(loaded, FunctionSpec)
        self.assertFalse(loaded.code.protected)
        self.assertFalse(loaded.code.caches)

    def _const_roundtrip_code(self, code):
        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        kio = load_kio(buf)
        for op, arg in kio.codes[0].soften():
            if op == OpCode.CONST:
                return arg
        self.fail("no CONST in roundtripped code")

    def test_source_stamp_roundtrip(self):
        """the META source stamp is stored and read back, whole or absent"""
        code, _ = build_expr([['CONST', 1], ['RETURN', 1]])
        stamped = KioMeta(source_mtime_ns=1234567890123456789, source_size=77)
        buf = io.BytesIO()
        KioFile.from_codes([code], meta=stamped).write(buf)
        buf.seek(0)
        meta = KioFile.read_meta(buf)
        self.assertEqual(meta.source_mtime_ns, 1234567890123456789)
        self.assertEqual(meta.source_size, 77)
        buf.seek(0)
        self.assertEqual(load_kio(buf).meta, stamped)

        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        meta = KioFile.read_meta(buf)
        self.assertIsNone(meta.source_mtime_ns)
        self.assertIsNone(meta.source_size)

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
            if op == OpCode.CONST:
                loaded = arg
                break
        self.assertIsInstance(loaded, FunctionSpec)
        inner_expr = make_callable(loaded.code, ns_inner)
        self.assertEqual(demand_call(inner_expr), 7)


    def test_annotated_build_function_roundtrip(self):
        inner_code, _ = build_expr([
            ['CONST', 7],
            ['RETURN', 1],
        ], name='inner')
        fspec = FunctionSpec('inner', FormalsSpec(('a',)), inner_code)
        code, ns = build_expr([
            ['CONST', 'int'],
            ['CONST', fspec],
            ['BUILD_FUNCTION', ((), ('a',), 0, 0)],
            ['RETURN', 1],
        ], name='outer')
        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        kio = load_kio(buf)
        self.assertEqual(len(kio.codes), 1)
        fn = demand_call(make_callable(kio.codes[0], ns))
        self.assertEqual(fn.__annotations__, {'a': 'int'})


    def test_function_spec_extra_operand_roundtrip(self):
        """
        A nested body whose instructions carry EXTRA operand tuples
        (fused loads, calls) is written and read back intact
        """

        inner_code, _ = build_expr([
            ['FAST2', (0, 1, 0, 0)],
            ['BUILD_LIST', 2],
            ['RETURN', 1],
        ], local_syms=('a', 'b'), name='inner')
        fspec = FunctionSpec('inner', FormalsSpec(('a', 'b')), inner_code)
        code, _ = build_expr([
            ['CONST', fspec],
            ['RETURN', 1],
        ], name='outer')
        buf = io.BytesIO()
        dump_kio(buf, [code])
        buf.seek(0)
        kio = load_kio(buf)
        loaded = None
        for op, arg in kio.codes[0].soften():
            if op == OpCode.CONST:
                loaded = arg
                break
        self.assertIsInstance(loaded, FunctionSpec)
        rows = [(op, arg) for op, arg in loaded.code.soften()
                if op == OpCode.FAST2]
        self.assertEqual([(OpCode.FAST2, (0, 1, 0, 0))], rows)


    def test_macro_rules_literals_roundtrip_as_frozenset(self):
        """
        The literal set a macro-rules form quotes into its build call
        is a frozenset holding the macro name, before and after .kio
        """

        buf = io.BytesIO()
        compile_ki_source_to_kio(
            '(define-syntax twice (macro-rules []\n'
            '  [(twice x) `(+ ,x ,x)]))\n', buf)
        buf.seek(0)
        kio = load_kio(buf)
        found = [val for val in kio.pool if isinstance(val, frozenset)]
        self.assertEqual(1, len(found))
        self.assertIn(symbol('twice'), found[0])


    def test_count_style_args_survive_roundtrip(self):
        """
        AWAIT and OBJECT_DIVMOD carry a stack count in their arg, and
        the count comes back from the .kio
        """

        buf = io.BytesIO()
        compile_ki_source_to_kio(
            '(define f (lambda [x] (await x)))\n'
            '(define d (divmod 7 2))\n', buf)
        buf.seek(0)
        kio = load_kio(buf)
        rows = {}
        for code in kio.codes:
            for op, arg in code.soften():
                if isinstance(arg, FunctionSpec):
                    for iop, iarg in arg.code.soften():
                        rows[OpCode(iop)] = iarg
                else:
                    rows[OpCode(op)] = arg
        self.assertEqual(1, rows[OpCode.AWAIT])
        self.assertEqual(2, rows[OpCode.OBJECT_DIVMOD])


    def test_compile_ki_conditional_branch_roundtrip(self):
        """
        A body with an if/else keeps its jump and label rows through
        the .kio, every jump names a label the reloaded body carries,
        and dumping the reloaded body again is byte-identical
        """

        source = (
            '(define x 1)\n'
            '(if (== x 1) (print "one") (print "other"))\n')
        buf = io.BytesIO()
        compile_ki_source_to_kio(source, buf)
        first = buf.getvalue()
        kio = load_kio(io.BytesIO(first))

        rows = [(OpCode(op), arg) for code in kio.codes
                for op, arg in code.soften()]
        labels = [arg for op, arg in rows if op == OpCode.LABEL]
        jumps = [arg for op, arg in rows
                 if op != OpCode.LABEL and op.style == OpStyle.LABEL]
        self.assertTrue(labels, rows)
        self.assertTrue(jumps, rows)
        for target in jumps:
            self.assertIn(target, labels)

        again = io.BytesIO()
        dump_kio(again, kio.codes)
        self.assertEqual(first, again.getvalue())


    def test_compile_ki_function_body_with_call_roundtrip(self):
        """
        Compiling source whose function body calls something reaches
        the .kio and reloads with the call row intact
        """

        buf = io.BytesIO()
        compile_ki_source_to_kio(
            '(define k (lambda [a b] (print a b)))\n', buf)
        buf.seek(0)
        kio = load_kio(buf)
        specs = [arg for op, arg in kio.codes[0].soften()
                 if isinstance(arg, FunctionSpec)]
        self.assertEqual(1, len(specs))
        ops = [OpCode(op) for op, _ in specs[0].code.soften()]
        self.assertIn(OpCode.FAST2, ops)
        self.assertTrue(any('CALL' in op.name for op in ops), ops)
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


class TestPoolDedup(unittest.TestCase):
    """
    Scalar pool values share a slot by value, everything else by identity
    """

    def test_equal_ints_share_a_slot(self):
        pb = PoolBuilder()
        first = int.from_bytes(b'\x01', 'big', signed=True)
        second = 1
        self.assertEqual(pb.add(first), pb.add(second))
        self.assertEqual(pb.add(10 ** 20), pb.add(int('1' + '0' * 20)))
        self.assertEqual(len(pb.values), 2)


    def test_equal_strings_share_a_slot(self):
        pb = PoolBuilder()
        self.assertEqual(pb.add('print'), pb.add(''.join(['pr', 'int'])))
        self.assertEqual(pb.add(b'ab'), pb.add(bytes([97, 98])))
        self.assertEqual(len(pb.values), 2)


    def test_type_and_sign_keep_separate_slots(self):
        pb = PoolBuilder()
        slots = {pb.add(v) for v in (1, 1.0, True, 0.0, -0.0, 1 + 0j)}
        self.assertEqual(len(slots), 6)
        self.assertEqual(pb.add(float('nan')), pb.add(float('nan')))
        self.assertNotEqual(pb.add(symbol('a')), pb.add('a'))


class TestFastValidation(unittest.TestCase):
    """
    FAST slot bounds enforced on re-harden
    """

    def test_fast_index_rejected(self):
        nspec = NamespaceSpec(None, ('a',), ())
        instr = [(OpCode.FAST, 1)]
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


    def _good_bytes(self):
        code, _ = build_expr([
            ['CONST', 42],
            ['RETURN', 1],
        ])
        buf = io.BytesIO()
        dump_kio(buf, [code])
        return bytearray(buf.getvalue())


    def test_validate_accepts_a_sound_file(self):
        header, sections = KioFile.validate(bytes(self._good_bytes()))
        self.assertEqual(header.magic, KIO_MAGIC)
        self.assertEqual(
            set(sections), {SectionTag.POOL, SectionTag.CODE, SectionTag.META})


    def test_validate_rejects_truncated_header(self):
        with self.assertRaises(ValueError):
            KioFile.validate(bytes(self._good_bytes()[:16]))


    def test_validate_rejects_truncated_body(self):
        data = self._good_bytes()
        with self.assertRaises(ValueError):
            KioFile.validate(bytes(data[:len(data) // 2]))


    def test_validate_rejects_missing_sections(self):
        data = self._good_bytes()
        # directory count sits after 8s + 5 u32 + Q in the header
        struct.pack_into('<I', data, 8 + 5 * 4 + 8, 0)
        with self.assertRaises(ValueError):
            KioFile.validate(bytes(data))


    def test_validate_rejects_section_past_end(self):
        data = self._good_bytes()
        header = KioHeader.from_bytes(data)
        # first directory entry: 4s tag, Q offset, Q length
        struct.pack_into('<Q', data, header.section_dir_offset + 4 + 8,
                         len(data) * 4)
        with self.assertRaises(ValueError):
            KioFile.validate(bytes(data))


    def test_read_meta_on_truncated_file_raises_value_error(self):
        with self.assertRaises(ValueError):
            KioFile.read_meta(io.BytesIO(bytes(self._good_bytes()[:16])))


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
