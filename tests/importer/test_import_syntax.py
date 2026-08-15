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
tests.importer.test_import_syntax

Cross-module import of a syntax transformer. A module defining a macro and
a second module importing it by name, so the macro is available to the
consumer at its own compile time.

These pin the contract that survives the move to prefixed binding keys:
whatever name a transformer is bound under, importing it by the name the
author wrote must leave it usable as syntax in the importing module.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


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
(define-syntax doubled
  (syntax-rules []
    [(doubled x) (* x 2)]))

(define plain-value 7)
"""


class SyntaxImportCase(unittest.IsolatedAsyncioTestCase):
    """
    Harness writing .ki modules into a throwaway directory on sys.path
    """

    def setUp(self):
        uninstall()
        install()
        self._tmpdir = tempfile.mkdtemp(prefix='koskari_syntax_import_')
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
        """
        Write one .ki module into the temporary directory

        :param modname: module name without extension
        :param content: koskari source text
        :return: path written
        """

        path = os.path.join(self._tmpdir, modname + '.ki')
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(content)
        return path


class TestImportSyntax(SyntaxImportCase):


    async def test_imported_syntax_expands_in_consumer(self):
        self.write_ki('ki_syn_provider', PROVIDER)
        self.write_ki('ki_syn_user', """
        (import-from ki_syn_provider doubled)
        (define result (doubled 21))
        """)

        mod = importlib.import_module('ki_syn_user')
        self.assertEqual(42, mod.result)


    async def test_imported_syntax_is_bound_in_consumer(self):
        self.write_ki('ki_syn_provider2', PROVIDER)
        self.write_ki('ki_syn_user2', """
        (import-from ki_syn_provider2 doubled)
        (define result (doubled 4))
        """)

        mod = importlib.import_module('ki_syn_user2')
        self.assertEqual(8, mod.result)


    async def test_renamed_syntax_expands_under_new_name(self):
        self.write_ki('ki_syn_provider3', PROVIDER)
        self.write_ki('ki_syn_user3', """
        (import-from ki_syn_provider3 [doubled as: twiced])
        (define result (twiced 5))
        """)

        mod = importlib.import_module('ki_syn_user3')
        self.assertEqual(10, mod.result)


    async def test_syntax_and_value_import_together(self):
        self.write_ki('ki_syn_provider4', PROVIDER)
        self.write_ki('ki_syn_user4', """
        (import-from ki_syn_provider4 doubled plain-value)
        (define result (doubled plain-value))
        """)

        mod = importlib.import_module('ki_syn_user4')
        self.assertEqual(14, mod.result)


# The end.
