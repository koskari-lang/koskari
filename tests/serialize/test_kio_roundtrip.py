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

import os
import tempfile
import unittest

from koskari.lib import NamespaceSpec, OpCode, demand_call
from koskari.serialize.incremental import compile_ki_file_to_kio
from koskari.serialize.kio import dump_kio, load_kio
from koskari.serialize.softcode import reharden_code, soften_code
from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_compile


class TestKioRoundTrip(unittest.TestCase):
    """
    dump_kio / load_kio identity for softened bytecode bodies
    """

    def test_const_expression_roundtrip(self):
        code, ns = build_expr([
            ['CONST', 42],
            ['RETURN', 1],
        ])
        instr, lc = soften_code(code)
        bodies = [{
            'nspec': code.namespace_spec,
            'name': 'e0',
            'filename': 'x.ki',
            'instructions': instr,
            'label_count': lc,
            'yields': False,
        }]
        fd, path = tempfile.mkstemp(suffix='.kio')
        os.close(fd)
        try:
            dump_kio(path, bodies)
            payload = load_kio(path)
            self.assertEqual(len(payload['bodies']), 1)
            b0 = payload['bodies'][0]
            c2 = reharden_code(
                b0['nspec'],
                b0['instructions'],
                b0['name'],
                b0['filename'])
            expr = make_callable(c2, ns)
            self.assertEqual(demand_call(expr), 42)
        finally:
            os.unlink(path)


class TestSourceFilenameIndirection(unittest.TestCase):
    """
    Embedded body filename can differ from the compile-time path
    """

    def test_compile_ki_embeds_source_filename(self):
        fd, ki_path = tempfile.mkstemp(suffix='.ki')
        os.write(fd, b'(define _sf_indir 0)\n')
        os.close(fd)
        fd2, kio_path = tempfile.mkstemp(suffix='.kio')
        os.close(fd2)
        try:
            logical = 'pkg/module.ki'
            compile_ki_file_to_kio(
                ki_path, kio_path, source_filename=logical)
            payload = load_kio(kio_path)
            self.assertEqual(len(payload['bodies']), 1)
            self.assertEqual(payload['bodies'][0]['filename'], logical)
            c = reharden_code(
                payload['bodies'][0]['nspec'],
                payload['bodies'][0]['instructions'],
                payload['bodies'][0]['name'],
                payload['bodies'][0]['filename'])
            self.assertEqual(c.filename, logical)
        finally:
            os.unlink(ki_path)
            os.unlink(kio_path)


class TestFastValidation(unittest.TestCase):
    """
    FAST slot bounds enforced on re-harden
    """

    def test_fast_index_rejected(self):
        nspec = NamespaceSpec(None, ('a',), ())
        instr = [(OpCode.KOS_OP_FAST, 1)]
        with self.assertRaises(ValueError) as ctx:
            reharden_code(nspec, instr, 'x', 'y.ki')
        self.assertIn('FAST', str(ctx.exception))


class TestKioMalformed(unittest.TestCase):
    """
    Reject corrupt inputs
    """

    def test_bad_magic(self):
        fd, path = tempfile.mkstemp(suffix='.kio')
        os.write(fd, b'XXXX' + b'\x00' * 46)
        os.close(fd)
        try:
            with self.assertRaises(ValueError) as ctx:
                load_kio(path)
            self.assertIn('magic', str(ctx.exception).lower())
        finally:
            os.unlink(path)


# The end.
