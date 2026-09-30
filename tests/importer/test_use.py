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
tests.importer.test_use

The use form: feature-family import. A stem brings every facet of a
feature -- the runtime binding and its compile/ and layout/ spellings --
bound under the stem, with renames substituting the whole family, and
exact from-import behavior on modules with no facet keys.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from textwrap import dedent

import importlib
import os
import shutil
import sys
import tempfile
import unittest

import pytest

from koskari.importer import install, uninstall


pytestmark = pytest.mark.phase_importer


PROVIDER = """
(define-syntax doubled (syntax-rules [] [(doubled x) (* x 2)]))
(define doubled-name "doubled")
(define plain-value 7)
"""


class UseCase(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_use_')
        self._prev_path = sys.path[:]
        sys.path.insert(0, self._tmpdir)


    def tearDown(self):
        uninstall()
        install()
        sys.path[:] = self._prev_path
        to_remove = [
            name for name, mod in list(sys.modules.items())
            if hasattr(mod, '__file__')
            and mod.__file__
            and mod.__file__.startswith(self._tmpdir)
        ]
        for name in to_remove:
            del sys.modules[name]
        shutil.rmtree(self._tmpdir, ignore_errors=True)


    def write_ki(self, modname, content):
        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(dedent(content))
        return path


class TestUse(UseCase):


    async def test_use_imports_the_family(self):
        # one stem carries the macro facet and the sibling runtime value
        self.write_ki('ki_use_prov', PROVIDER)
        self.write_ki('ki_use_user', """
        (use ki_use_prov doubled plain-value)
        (define result (doubled plain-value))
        """)

        mod = importlib.import_module('ki_use_user')
        self.assertEqual(mod.result, 14)


    async def test_use_runtime_facet_alongside_macro(self):
        # a feature with both a runtime and a compile facet brings both
        self.write_ki('ki_use_prov2', """
        (define-syntax spoken (syntax-rules [] [(spoken x) x]))
        (define spoken "aloud")
        """)
        self.write_ki('ki_use_user2', """
        (use ki_use_prov2 spoken)
        (define result #[(spoken 42) spoken])
        """)

        mod = importlib.import_module('ki_use_user2')
        self.assertEqual(mod.result, [42, "aloud"])


    async def test_use_rename_substitutes_the_family(self):
        self.write_ki('ki_use_prov3', PROVIDER)
        self.write_ki('ki_use_user3', """
        (use ki_use_prov3 [doubled as: twiced])
        (define result (twiced 21))
        """)

        mod = importlib.import_module('ki_use_user3')
        self.assertEqual(mod.result, 42)


    async def test_use_missing_feature_directed_error(self):
        self.write_ki('ki_use_prov4', PROVIDER)
        self.write_ki('ki_use_user4', """
        (use ki_use_prov4 nonesuch)
        """)

        with self.assertRaises(ImportError) as caught:
            importlib.import_module('ki_use_user4')
        msg = str(caught.exception)
        self.assertIn("nonesuch", msg)
        self.assertIn("compile/nonesuch", msg)


    async def test_use_missing_literal_stem_error_names_one_spelling(self):
        # a literal-facet stem probes exactly one candidate; the error
        # must not enumerate spellings that were never tried
        self.write_ki('ki_use_prov4b', PROVIDER)
        self.write_ki('ki_use_user4b', """
        (use ki_use_prov4b compile/nonesuch)
        """)

        with self.assertRaises(ImportError) as caught:
            importlib.import_module('ki_use_user4b')
        msg = str(caught.exception)
        self.assertIn("compile/nonesuch", msg)
        self.assertNotIn("compile/compile/nonesuch", msg)
        self.assertNotIn("layout/compile/nonesuch", msg)


    async def test_use_python_module_degrades_to_from_import(self):
        self.write_ki('ki_use_user5', """
        (use os.path join)
        (define result (join "a" "b"))
        """)

        mod = importlib.import_module('ki_use_user5')
        self.assertEqual(mod.result, os.path.join("a", "b"))


    async def test_use_literal_prefixed_stem(self):
        self.write_ki('ki_use_prov6', PROVIDER)
        self.write_ki('ki_use_user6', """
        (use ki_use_prov6 compile/doubled)
        (define result (doubled 4))
        """)

        mod = importlib.import_module('ki_use_user6')
        self.assertEqual(mod.result, 8)


    async def test_use_prefixed_bind_is_literal(self):
        # a rename onto a facet-prefixed bind is a literal single
        # binding: the bare candidate lands there, and no doubled-prefix
        # facet keys the compiler never declared are stored
        self.write_ki('ki_use_prov10', """
        (define-syntax spoken (syntax-rules [] [(spoken x) x]))
        (define spoken "aloud")
        """)
        self.write_ki('ki_use_user10', """
        (use ki_use_prov10 [spoken as: compile/said])
        """)

        mod = importlib.import_module('ki_use_user10')
        ns = vars(mod)
        self.assertEqual(ns['compile/said'], "aloud")
        self.assertNotIn('compile/compile/said', ns)
        self.assertNotIn('layout/compile/said', ns)


    async def test_use_nested_scope_rejected(self):
        from koskari.exceptions import SpecialError
        self.write_ki('ki_use_prov7', PROVIDER)
        self.write_ki('ki_use_user7', """
        (define f (lambda f [] (begin (use ki_use_prov7 doubled) 1)))
        """)

        with self.assertRaises(SpecialError):
            importlib.import_module('ki_use_user7')


    async def test_use_extend_after_use(self):
        # an imported family composes with extend-syntax, like a local
        self.write_ki('ki_use_prov8', PROVIDER)
        self.write_ki('ki_use_user8', """
        (use ki_use_prov8 doubled)
        (extend-syntax doubled (syntax-rules [] [(doubled x y) (* x y)]))
        (define result #[(doubled 21) (doubled 6 7)])
        """)

        mod = importlib.import_module('ki_use_user8')
        self.assertEqual(mod.result, [42, 42])


class TestUseErrorFidelity(UseCase):
    """
    A stem miss is tolerated per facet, but a real failure inside a
    submodule's own import propagates as itself, matching import-from
    """


    async def test_broken_submodule_error_propagates(self):
        pkg = os.path.join(self._tmpdir, 'ki_use_pkg')
        os.makedirs(pkg)
        with open(os.path.join(pkg, '__init__.ki'), 'w') as fh:
            fh.write("(define marker 1)\n")
        with open(os.path.join(pkg, 'broken.ki'), 'w') as fh:
            fh.write('(raise (RuntimeError "boom inside broken"))\n')

        self.write_ki('ki_use_user9', """
        (use ki_use_pkg broken)
        """)

        with self.assertRaises(RuntimeError) as caught:
            importlib.import_module('ki_use_user9')
        self.assertIn("boom inside broken", str(caught.exception))


    async def test_submodule_import_error_propagates(self):
        """
        An ImportError raised by the submodule's own body is the
        submodule's failure, not a missing member, and propagates
        with its message
        """

        pkg = os.path.join(self._tmpdir, 'ki_use_pkg2')
        os.makedirs(pkg)
        with open(os.path.join(pkg, '__init__.ki'), 'w') as fh:
            fh.write("(define marker 1)\n")
        with open(os.path.join(pkg, 'needy.ki'), 'w') as fh:
            fh.write('(raise (ImportError "optional dependency missing"))\n')

        self.write_ki('ki_use_user10', """
        (use ki_use_pkg2 needy)
        """)

        with self.assertRaises(ImportError) as caught:
            importlib.import_module('ki_use_user10')
        self.assertIn("optional dependency missing", str(caught.exception))


class TestUseBare(UseCase):
    """
    The bare form: (use mod) lowers to import-all-from, and the seal-
    materialized __all__ carries every feature's facets
    """


    async def test_declared_module_materializes_all(self):
        self.write_ki('ki_useb_prov', dedent("""
        (declare-exports doubled plain-value)
        """) + PROVIDER)

        prov = importlib.import_module('ki_useb_prov')
        self.assertEqual(
            prov.__features__, ("doubled", "plain-value"))
        self.assertIn("compile/doubled", prov.__all__)
        self.assertIn("doubled-name", dir(prov))
        self.assertNotIn("doubled-name", prov.__all__)


    async def test_bare_use_carries_declared_features(self):
        self.write_ki('ki_useb_prov2', dedent("""
        (declare-exports doubled plain-value)
        """) + PROVIDER)
        self.write_ki('ki_useb_user2', """
        (use ki_useb_prov2)
        (define result (doubled plain-value))
        """)

        mod = importlib.import_module('ki_useb_user2')
        self.assertEqual(mod.result, 14)


    async def test_bare_use_undeclared_module_falls_back(self):
        # no declaration: the star fallback still carries facet keys,
        # since they are ordinary non-underscore attrs
        self.write_ki('ki_useb_prov3', PROVIDER)
        self.write_ki('ki_useb_user3', """
        (use ki_useb_prov3)
        (define result (doubled 5))
        """)

        mod = importlib.import_module('ki_useb_user3')
        self.assertEqual(mod.result, 10)


    async def test_none_valued_export_materializes(self):
        # presence, not non-None-ness: a declared export bound to None
        # is a real attribute and belongs in __all__
        self.write_ki('ki_useb_prov5', """
        (declare-exports flag)
        (define flag None)
        """)

        prov = importlib.import_module('ki_useb_prov5')
        self.assertIn("flag", prov.__all__)


    async def test_literal_prefixed_declaration(self):
        # a prefixed declare-exports entry is a literal single-facet
        # export
        self.write_ki('ki_useb_prov4', dedent("""
        (declare-exports compile/doubled)
        """) + PROVIDER)

        prov = importlib.import_module('ki_useb_prov4')
        self.assertEqual(prov.__all__, ("compile/doubled",))


# The end.
