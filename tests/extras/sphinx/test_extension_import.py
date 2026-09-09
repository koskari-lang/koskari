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
tests.extras.sphinx.test_extension_import

Import koskari.extras.sphinx without requiring Sphinx at import time
"""

import importlib
import unittest

import pytest


pytestmark = pytest.mark.phase_extras


class TestSphinxExtensionImport(unittest.TestCase):

    def test_import_koskari_sphinx_module(self):
        mod = importlib.import_module('koskari.extras.sphinx')
        self.assertTrue(callable(mod.setup))


    def test_autodoc_submodule_with_sphinx_installed(self):
        pytest.importorskip('sphinx')
        mod = importlib.import_module('koskari.extras.sphinx.autodoc')
        self.assertTrue(hasattr(mod, 'register_autodocumenters'))
        self.assertTrue(hasattr(mod, 'KosFunctionDocumenter'))


# The end.
