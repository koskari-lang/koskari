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
Importer path for precompiled .kio modules.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import importlib.util
import os
import tempfile
import types
import unittest

import pytest

from koskari.importer import KoskariSourceFileLoader, install, uninstall
from koskari.serialize.incremental import compile_ki_file_to_kio


pytestmark = pytest.mark.phase_importer


class TestKioImporterParity(unittest.TestCase):
    """
    .ki compile to .kio then exec matches direct .ki semantics for a define
    """

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_kio_')
        self._prev_path = __import__('sys').path[:]
        __import__('sys').path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        __import__('sys').path[:] = self._prev_path
        to_remove = [
            name for name, mod in list(__import__('sys').modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del __import__('sys').modules[name]
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def _write(self, name, source):
        path = os.path.join(self._tmpdir, name)
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(source)
        return path


    def _load(self, modname, path):
        loader = KoskariSourceFileLoader(modname, path)
        spec = importlib.util.spec_from_file_location(
            modname, path, loader=loader)
        module = types.ModuleType(modname)
        module.__name__ = modname
        module.__package__ = None
        module.__spec__ = spec
        loader.exec_module_demand(module)
        return module


    def _both(self, stem, source):
        """
        Load ``source`` once from .ki and once from a .kio compiled from
        it; return the (ki module, kio module) pair
        """

        ki_path = self._write(stem + '_src.ki', source)
        kio_path = os.path.join(self._tmpdir, stem + '_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)
        return (self._load(stem + '_ki', ki_path),
                self._load(stem + '_kio', kio_path))


    def test_docstring_via_kio_matches_ki(self):
        ki, kio = self._both('doc', '"""the module doc"""\n(define x 1)\n')
        self.assertEqual(ki.__doc__, "the module doc")
        self.assertEqual(kio.__doc__, ki.__doc__)
        self.assertEqual(kio.x, 1)


    def test_rules_transformers_via_kio_match_ki(self):
        """
        Modules defining macro-rules and special-rules transformers
        reach the .kio and expand the same way from it
        """

        source = (
            "(define-syntax twice (macro-rules []\n"
            "  [(twice x) `(+ ,x ,x)]))\n"
            "(define-syntax ident (special-rules []\n"
            "  [(c (ident x)) x.datum]))\n"
            "(define a (twice 21))\n"
            "(define b (ident 7))\n")
        ki, kio = self._both('rules', source)
        self.assertEqual(42, ki.a)
        self.assertEqual(7, ki.b)
        self.assertEqual(ki.a, kio.a)
        self.assertEqual(ki.b, kio.b)


    def test_divmod_via_kio_matches_ki(self):
        """
        A divmod call keeps its operand count through the .kio
        """

        ki, kio = self._both('divmod', "(define d (divmod 7 2))\n")
        self.assertEqual((3, 1), ki.d)
        self.assertEqual(ki.d, kio.d)


    def test_function_body_via_kio_matches_ki(self):
        """
        A function whose body calls and builds from its formals runs the
        same from .kio as from .ki
        """

        source = (
            "(define k (lambda [a b] #[a b (+ a b)]))\n"
            "(define r (k 1 2))\n"
            "(define r2 (k 3 4))\n")
        ki, kio = self._both('fnbody', source)
        self.assertEqual([1, 2, 3], ki.r)
        self.assertEqual([3, 4, 7], ki.r2)
        self.assertEqual(ki.r, kio.r)
        self.assertEqual(ki.r2, kio.r2)


    def test_pragma_via_kio_matches_ki(self):
        source = (
            "(pragma [compiler/call-brackets 'warn])\n"
            "(define setting (pragma compiler/call-brackets))\n")
        ki, kio = self._both('pragma', source)
        self.assertEqual(kio.setting, ki.setting)
        self.assertIsNotNone(kio.__pragma__)
        self.assertEqual(
            kio.__pragma__.get('compiler/call-brackets'),
            ki.__pragma__.get('compiler/call-brackets'))


    def test_exports_via_kio_matches_ki(self):
        source = "(declare-exports alpha)\n(define alpha 1)\n(define beta 2)\n"
        ki, kio = self._both('exports', source)
        self.assertEqual(ki.__all__, ('alpha',))
        self.assertEqual(kio.__all__, ki.__all__)


    def test_ellipsis_const_via_kio(self):
        _ki, kio = self._both('ellipsis', "(define e ...)\n")
        self.assertIs(kio.e, ...)


    def test_kio_bodies_keep_source_code_settings(self):
        """
        top-level bodies compile with the pragma defaults, no cache slots
        and no read-only sealing, and load back the same way
        """

        from koskari.serialize import load_kio

        ki_path = self._write('settings_src.ki', "(define a 1)\n(define b 2)\n")
        kio_path = os.path.join(self._tmpdir, 'settings_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)
        codes = load_kio(kio_path).codes
        self.assertEqual(len(codes), 2)
        for code in codes:
            self.assertFalse(code.caches)
            self.assertFalse(code.protected)


    def _import(self, modname):
        importlib.invalidate_caches()
        from koskari.importer import KoskariPathFinder
        KoskariPathFinder.invalidate_caches()
        return importlib.import_module(modname)


    def test_fresh_kio_beside_source_is_loaded(self):
        """a .kio whose stamp matches its sibling .ki is the one imported"""
        ki_path = self._write('fresh_pick.ki', "(define who 1)\n")
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'fresh_pick.kio'))
        module = self._import('fresh_pick')
        self.assertEqual(module.who, 1)
        self.assertTrue(module.__file__.endswith('.kio'), module.__file__)


    def test_truncated_kio_yields_to_source(self):
        """an unreadable .kio beside its .ki is passed over for the source"""
        ki_path = self._write('cut_pick.ki', "(define who 1)\n")
        kio_path = os.path.join(self._tmpdir, 'cut_pick.kio')
        compile_ki_file_to_kio(ki_path, kio_path)
        with open(kio_path, 'rb') as fh:
            head = fh.read(16)
        with open(kio_path, 'wb') as fh:
            fh.write(head)
        module = self._import('cut_pick')
        self.assertEqual(module.who, 1)
        self.assertTrue(module.__file__.endswith('.ki'), module.__file__)


    def test_stale_kio_yields_to_source(self):
        """a .ki edited after its .kio was compiled is the one imported"""
        ki_path = self._write('stale_pick.ki', "(define who 1)\n")
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'stale_pick.kio'))
        self._write('stale_pick.ki', "(define who 2)\n")
        later = os.stat(ki_path).st_mtime + 10
        os.utime(ki_path, (later, later))
        module = self._import('stale_pick')
        self.assertEqual(module.who, 2)
        self.assertTrue(module.__file__.endswith('.ki'), module.__file__)


    def test_unstamped_kio_yields_to_source(self):
        """a .kio with no source stamp never shadows a sibling .ki"""
        from koskari.serialize import compile_ki_source_to_kio

        self._write('unstamped_pick.ki', "(define who 2)\n")
        compile_ki_source_to_kio(
            "(define who 1)\n",
            os.path.join(self._tmpdir, 'unstamped_pick.kio'))
        module = self._import('unstamped_pick')
        self.assertEqual(module.who, 2)


    def test_lone_kio_loads_without_source(self):
        """a .kio with no sibling .ki loads as it is"""
        ki_path = self._write('lone_src.ki', "(define who 1)\n")
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'lone_pick.kio'))
        module = self._import('lone_pick')
        self.assertEqual(module.who, 1)


    def test_define_via_kio_matches_ki(self):
        ki_path = os.path.join(self._tmpdir, 'parity_src.ki')
        with open(ki_path, 'w', encoding='utf-8') as fh:
            fh.write('(define parity_answer 42)\n')

        kio_path = os.path.join(self._tmpdir, 'parity_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)

        loader = KoskariSourceFileLoader('parity_kio', kio_path)
        spec = importlib.util.spec_from_file_location(
            'parity_kio', kio_path, loader=loader)
        module = types.ModuleType('parity_kio')
        module.__name__ = 'parity_kio'
        module.__package__ = None
        module.__spec__ = spec
        loader.exec_module_demand(module)
        self.assertEqual(42, module.parity_answer)


# The end.
