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
import re
import subprocess
import sys
import tempfile
import unittest

import pytest

from koskari.importer import KoskariSourceFileLoader, install, uninstall
from koskari.lib import FormalsSpec, FunctionSpec, cons, demand_call, nil
from koskari.serialize.codex import KioFile
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
        module = importlib.util.module_from_spec(spec)
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


    def _package(self, name, files):
        """
        Write a package directory under the temp dir from a mapping of
        file name to source; return its path
        """

        pkg = os.path.join(self._tmpdir, name)
        os.mkdir(pkg)
        for fname, source in files.items():
            path = os.path.join(pkg, fname)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8') as fh:
                fh.write(source)
        return pkg


    def test_kio_build_inherits_the_package_pragma(self):
        """
        A submodule compiled to .kio is compiled under the package's
        pragma, as a source load of it is: the package turns asserts
        off, so the submodule's failing assert is dropped at build
        """

        pkg = self._package('kiopkg_pragma', {
            '__init__.ki': "(package-pragma [compiler/enable-asserts False])\n",
            'sub.ki': '(assert False "kept by the default pragma")\n'
                      '(var x 1)\n',
        })
        compile_ki_file_to_kio(os.path.join(pkg, 'sub.ki'),
                               os.path.join(pkg, 'sub.kio'))
        mod = importlib.import_module('kiopkg_pragma.sub')
        self.assertEqual(mod.x, 1)


    def test_kio_build_of_a_nested_package_init_inherits_the_package_pragma(self):
        """
        A nested package's own __init__ compiled to .kio is compiled
        under the parent package's pragma, as a source load of it is
        """

        pkg = self._package('kiopkg_nested', {
            '__init__.ki': "(package-pragma [compiler/enable-asserts False])\n",
            'inner/__init__.ki':
                '(assert False "kept by the default pragma")\n'
                '(var x 1)\n',
        })
        compile_ki_file_to_kio(os.path.join(pkg, 'inner', '__init__.ki'),
                               os.path.join(pkg, 'inner', '__init__.kio'))
        mod = importlib.import_module('kiopkg_nested.inner')
        self.assertEqual(mod.x, 1)


    def test_kio_build_resolves_a_relative_import(self):
        """
        A submodule's relative import needs __package__, which the build
        sets from the file's place in its package
        """

        pkg = self._package('kiopkg_rel', {
            '__init__.ki': "",
            'helper.ki': "(var helper 41)\n",
            'rel.ki': "(import-from .helper helper)\n(var y (+ helper 1))\n",
        })
        compile_ki_file_to_kio(os.path.join(pkg, 'rel.ki'),
                               os.path.join(pkg, 'rel.kio'))
        mod = importlib.import_module('kiopkg_rel.rel')
        self.assertEqual(mod.y, 42)


    def test_kio_build_finds_the_package_from_any_working_directory(self):
        """
        A submodule's relative import resolves at build when neither the
        working directory nor sys.path holds the package root's parent:
        the build puts that directory on the path itself
        """

        import warnings

        root = os.path.join(self._tmpdir, 'elsewhere')
        os.mkdir(root)
        pkg = os.path.join(root, 'kiopkg_far')
        os.mkdir(pkg)
        for fname, source in {
                '__init__.ki': "",
                'helper.ki': "(var helper 41)\n",
                'rel.ki': "(import-from .helper helper)\n"
                          "(var y (+ helper 1))\n"}.items():
            with open(os.path.join(pkg, fname), 'w', encoding='utf-8') as fh:
                fh.write(source)
        self.assertNotIn(root, sys.path)

        before = list(sys.path)
        with warnings.catch_warnings():
            warnings.simplefilter('error')
            compile_ki_file_to_kio(os.path.join(pkg, 'rel.ki'),
                                   os.path.join(pkg, 'rel.kio'))
        self.assertEqual(before, sys.path)
        self.assertEqual(2, len(KioFile.read(
            os.path.join(pkg, 'rel.kio')).codes))


    def test_kio_holds_plain_code_names(self):
        """
        A function a macro template names (the lambda each expands to) is
        serialized under its plain code name, with no expansion mark:
        the name a traceback and __name__ show
        """
        src = ("def h [words]:\n"
               "    (list (each [w words] (len w)))\n")
        ki, kio = self._both('plain_names', src)
        for mod in (ki, kio):
            inner = [c for c in mod.h.code.constants_pool
                     if isinstance(c, FunctionSpec)]
            self.assertEqual(['each'], [c.code.name for c in inner])


    def test_docstring_via_kio_matches_ki(self):
        ki, kio = self._both('doc', '"""the module doc"""\n(var x 1)\n')
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
            "(var a (twice 21))\n"
            "(var b (ident 7))\n")
        ki, kio = self._both('rules', source)
        self.assertEqual(42, ki.a)
        self.assertEqual(7, ki.b)
        self.assertEqual(ki.a, kio.a)
        self.assertEqual(ki.b, kio.b)


    def test_syntax_match_arm_with_an_extern_via_kio_matches_ki(self):
        """
        A syntax-match transformer whose arm names an extern reaches the
        .kio and expands the same way from it
        """

        source = (
            "(var match-subject int)\n"
            "(define-syntax ext (syntax-match\n"
            "  [(ext (TYPE match-subject)) \"int-arm\"]\n"
            "  [(ext (VALUE y)) \"other-arm\"]))\n"
            "(var a (ext 7))\n"
            "(var b (ext \"s\"))\n")
        ki, kio = self._both('ext', source)
        self.assertEqual("int-arm", ki.a)
        self.assertEqual("other-arm", ki.b)
        self.assertEqual(ki.a, kio.a)
        self.assertEqual(ki.b, kio.b)


    def test_matchers_via_kio_hold_no_gensym_but_the_subject(self):
        """
        A module with define-syntax matchers and a runtime match reaches
        the .kio and runs the same from it. The only gensym spellings in
        its pool are the subject formals of the define-syntax matcher
        functions: the captures live on the stack, not in locals
        """

        source = (
            "(define-syntax swap-pairs (syntax-rules []\n"
            "  [(swap-pairs (a b) ...) '((b a) ...)]))\n"
            "(define-syntax pick (syntax-match\n"
            "  [(pick (GROUP g (VALUE x) (VALUE y))) \"group\"]\n"
            "  [(pick (VALUE v)) \"one\"]))\n"
            "(define [classify data]\n"
            "  (match data\n"
            "    [[#[a #[b c] *: rest]] #(a b c rest)]\n"
            "    [[(or #[x 1] #[5 y])] #(\"or\" x)]\n"
            "    [[#[k (OPTIONAL z)]] #(\"opt\" k)]\n"
            "    [else: \"none\"]))\n"
            "(var r1 (classify #[1 #[2 3] 4 5]))\n"
            "(var r2 (classify #[7 1]))\n"
            "(var r3 (classify #[8]))\n"
            "(var r4 (swap-pairs (1 2) (3 4)))\n"
            "(var r5 (pick 1 2))\n"
            "(var r6 (pick 1))\n")
        ki, kio = self._both('matchers', source)
        self.assertEqual((1, 2, 3, (4, 5)), ki.r1)
        self.assertEqual(("or", 7), ki.r2)
        self.assertEqual(("opt", 8), ki.r3)
        self.assertEqual("group", ki.r5)
        self.assertEqual("one", ki.r6)
        for name in ("r1", "r2", "r3", "r4", "r5", "r6"):
            self.assertEqual(getattr(ki, name), getattr(kio, name), name)
        self.assertEqual(
            cons(cons(2, 1, nil), cons(4, 3, nil), nil), kio.r4)

        pool = KioFile.read(
            os.path.join(self._tmpdir, 'matchers_out.kio')).pool
        # a gensym spells its mark in unpadded hex; the positional repeat
        # slot names are plain strings that also carry a "#"
        spelled = {
            value for value in pool
            if isinstance(value, str) and re.search(r'#[0-9a-f]+$', value)
            and not value.startswith('_repeat#')}
        formals = {
            str(name) for value in pool if isinstance(value, FormalsSpec)
            for name in value.pos or ()}
        self.assertEqual(3, len(spelled))
        self.assertLessEqual(spelled, formals)


    GENERATING = (
        "(define-syntax mk-getter (syntax-rules []\n"
        "  [(mk-getter name val)\n"
        "   (begin (var secret val)\n"
        "          (define-syntax name (macro-rules []\n"
        "            [(name) #`secret])))]))\n"
        "(var secret 100)\n"
        "(mk-getter get-secret 8)\n"
        "(var got (get-secret))\n")


    def test_generated_macro_via_kio_matches_ki(self):
        """
        A macro a template defines keeps reaching the outer expansion's
        binder from the .kio, which holds no gensym: the reference is
        resolved to that binder when the module compiles, before the
        .kio is written
        """

        ki, kio = self._both('generating', self.GENERATING)
        self.assertEqual(8, ki.got)
        self.assertEqual(100, ki.secret)
        self.assertEqual(ki.got, kio.got)
        self.assertEqual(ki.secret, kio.secret)


    def test_generated_macro_imported_from_kio(self):
        """
        Another module imports the generated macro from the compiled
        module. The macro's free name has no binding in the importing
        module, so it resolves at the use site, as it does from a .ki
        """

        ki_path = self._write('gen_lib.ki', self.GENERATING)
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'gen_lib.kio'))
        os.unlink(ki_path)
        user_path = self._write(
            'gen_user.ki',
            "(import-from gen_lib compile/get-secret)\n"
            "(var secret 555)\n"
            "(var here (get-secret))\n")
        user = self._load('gen_user', user_path)
        self.assertEqual(555, user.here)
        self.assertEqual(555, user.secret)


    def test_generated_macro_kio_in_fresh_process(self):
        """
        The compiled module loads and runs in an interpreter whose mark
        seed differs from the one that compiled it
        """

        ki_path = self._write('gen_fresh.ki', self.GENERATING)
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'gen_fresh.kio'))
        os.unlink(ki_path)
        script = (
            "import sys\n"
            f"sys.path.insert(0, {self._tmpdir!r})\n"
            "import koskari\n"
            "import gen_fresh\n"
            "print(gen_fresh.got, gen_fresh.secret)\n")
        proc = subprocess.run(
            [sys.executable, '-c', script],
            capture_output=True, text=True, timeout=120)
        self.assertEqual(0, proc.returncode, proc.stderr)
        self.assertEqual("8 100", proc.stdout.strip())


    def test_generated_macro_imported_from_kio_in_fresh_process(self):
        """
        An interpreter that did not compile the module imports the
        generated macro from its .kio. The template carries plain
        symbols, so the macro's free name resolves at the use site
        """

        ki_path = self._write('gen_src.ki', self.GENERATING)
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'gen_src.kio'))
        os.unlink(ki_path)
        self._write(
            'gen_use.ki',
            "(import-from gen_src compile/get-secret)\n"
            "(var secret 555)\n"
            "(var here (get-secret))\n")
        script = (
            "import sys\n"
            f"sys.path.insert(0, {self._tmpdir!r})\n"
            "import koskari\n"
            "import gen_use\n"
            "print(gen_use.here)\n")
        proc = subprocess.run(
            [sys.executable, '-c', script],
            capture_output=True, text=True, timeout=120)
        self.assertEqual(0, proc.returncode, proc.stderr)
        self.assertEqual("555", proc.stdout.strip())


    def test_divmod_via_kio_matches_ki(self):
        """
        A divmod call keeps its operand count through the .kio
        """

        ki, kio = self._both('divmod', "(var d (divmod 7 2))\n")
        self.assertEqual((3, 1), ki.d)
        self.assertEqual(ki.d, kio.d)


    def test_function_body_via_kio_matches_ki(self):
        """
        A function whose body calls and builds from its formals runs the
        same from .kio as from .ki
        """

        source = (
            "(def k [a b] #[a b (+ a b)])\n"
            "(var r (k 1 2))\n"
            "(var r2 (k 3 4))\n")
        ki, kio = self._both('fnbody', source)
        self.assertEqual([1, 2, 3], ki.r)
        self.assertEqual([3, 4, 7], ki.r2)
        self.assertEqual(ki.r, kio.r)
        self.assertEqual(ki.r2, kio.r2)


    def test_pragma_via_kio_matches_ki(self):
        source = (
            "(pragma [compiler/call-brackets 'warn])\n"
            "(var setting (pragma compiler/call-brackets))\n")
        ki, kio = self._both('pragma', source)
        self.assertEqual(kio.setting, ki.setting)
        self.assertIsNotNone(kio.__pragma__)
        self.assertEqual(
            kio.__pragma__.get('compiler/call-brackets'),
            ki.__pragma__.get('compiler/call-brackets'))


    def test_exports_via_kio_matches_ki(self):
        source = "(declare-exports alpha)\n(var alpha 1)\n(var beta 2)\n"
        ki, kio = self._both('exports', source)
        self.assertEqual(ki.__all__, ('alpha',))
        self.assertEqual(kio.__all__, ki.__all__)


    def test_ellipsis_const_via_kio(self):
        _ki, kio = self._both('ellipsis', "(var e ...)\n")
        self.assertIs(kio.e, ...)


    def test_kio_bodies_keep_source_code_settings(self):
        """
        top-level bodies compile with the pragma defaults, no cache slots
        and no read-only sealing, and load back the same way
        """

        from koskari.serialize import load_kio

        ki_path = self._write('settings_src.ki', "(var a 1)\n(var b 2)\n")
        kio_path = os.path.join(self._tmpdir, 'settings_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)
        codes = load_kio(kio_path).codes
        self.assertEqual(len(codes), 2)
        for code in codes:
            self.assertFalse(code.caches)
            self.assertFalse(code.protected)


    def test_kio_function_code_placed_and_sealed(self):
        """
        a .kio module's function code sits in a sealed arena chunk, as
        it does under a source load
        """

        ki, kio = self._both('placed', "(def f [] 1)\n")
        self.assertTrue(ki.f.code.arena.sealed)
        self.assertIsNotNone(kio.f.code.arena)
        self.assertTrue(kio.f.code.arena.sealed)
        self.assertEqual(demand_call(kio.f), 1)


    def test_kio_function_code_sealed_before_first_body(self):
        """
        a .kio decodes every function before any body runs, so its code
        is already sealed while the module's own forms run
        """

        ki, kio = self._both(
            'early',
            "(def f [] 1)\n"
            "(var during f.code.arena.sealed)\n")
        self.assertIs(ki.during, False)
        self.assertIs(kio.during, True)


    def test_kio_pack_module_off_is_heap(self):
        """a .kio built with pack-module off loads its functions as heap"""
        _ki, kio = self._both(
            'unplaced',
            "(pragma [compiler/pack-module False])\n"
            "(def f [] 1)\n")
        self.assertIsNone(kio.f.code.arena)


    def test_kio_pack_module_toggle_mid_module(self):
        """
        a .kio records each function's placement as its build made it:
        placed before a mid-module toggle off, heap after it
        """

        ki, kio = self._both(
            'toggled',
            "(def before [] 1)\n"
            "(pragma [compiler/pack-module False])\n"
            "(def after [] 2)\n")
        for mod in (ki, kio):
            self.assertIsNotNone(mod.before.code.arena)
            self.assertTrue(mod.before.code.arena.sealed)
            self.assertIsNone(mod.after.code.arena)
            self.assertEqual(demand_call(mod.before), 1)
            self.assertEqual(demand_call(mod.after), 2)


    def _import(self, modname):
        importlib.invalidate_caches()
        return importlib.import_module(modname)


    def test_fresh_kio_beside_source_is_loaded(self):
        """a .kio whose stamp matches its sibling .ki is the one imported"""
        ki_path = self._write('fresh_pick.ki', "(var who 1)\n")
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'fresh_pick.kio'))
        module = self._import('fresh_pick')
        self.assertEqual(module.who, 1)
        self.assertTrue(module.__file__.endswith('.kio'), module.__file__)


    def test_truncated_kio_yields_to_source(self):
        """an unreadable .kio beside its .ki is passed over for the source"""
        ki_path = self._write('cut_pick.ki', "(var who 1)\n")
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
        ki_path = self._write('stale_pick.ki', "(var who 1)\n")
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'stale_pick.kio'))
        self._write('stale_pick.ki', "(var who 2)\n")
        later = os.stat(ki_path).st_mtime + 10
        os.utime(ki_path, (later, later))
        module = self._import('stale_pick')
        self.assertEqual(module.who, 2)
        self.assertTrue(module.__file__.endswith('.ki'), module.__file__)


    def test_unstamped_kio_yields_to_source(self):
        """a .kio with no source stamp never shadows a sibling .ki"""
        from koskari.serialize import compile_ki_source_to_kio

        self._write('unstamped_pick.ki', "(var who 2)\n")
        compile_ki_source_to_kio(
            "(var who 1)\n",
            os.path.join(self._tmpdir, 'unstamped_pick.kio'))
        module = self._import('unstamped_pick')
        self.assertEqual(module.who, 2)


    def test_lone_kio_loads_without_source(self):
        """a .kio with no sibling .ki loads as it is"""
        ki_path = self._write('lone_src.ki', "(var who 1)\n")
        compile_ki_file_to_kio(
            ki_path, os.path.join(self._tmpdir, 'lone_pick.kio'))
        module = self._import('lone_pick')
        self.assertEqual(module.who, 1)


    def test_define_via_kio_matches_ki(self):
        ki_path = os.path.join(self._tmpdir, 'parity_src.ki')
        with open(ki_path, 'w', encoding='utf-8') as fh:
            fh.write('(var parity_answer 42)\n')

        kio_path = os.path.join(self._tmpdir, 'parity_out.kio')
        compile_ki_file_to_kio(ki_path, kio_path)

        loader = KoskariSourceFileLoader('parity_kio', kio_path)
        spec = importlib.util.spec_from_file_location(
            'parity_kio', kio_path, loader=loader)
        module = importlib.util.module_from_spec(spec)
        loader.exec_module_demand(module)
        self.assertEqual(42, module.parity_answer)


# The end.
