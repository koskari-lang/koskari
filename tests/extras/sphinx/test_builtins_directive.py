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
tests.extras.sphinx.test_builtins_directive

KosBuiltinsDirective and pick_documenter integration
"""

import unittest
from unittest.mock import MagicMock

import pytest


pytest.importorskip('sphinx')

from sphinx.ext.autodoc import FunctionDocumenter, ModuleDocumenter

from koskari.extras.sphinx.autodoc import (
    KosFunctionDocumenter, KosSyntaxDocumenter, pick_documenter)
from koskari.extras.sphinx.builtins_directive import KosBuiltinsDirective
from koskari.lib import FormalsSpec
from koskari.matcher import CALLEDBY, MATCH, VALUE
from koskari.syntax import special
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_extras


def _make_syntax():
    pat = MATCH(CALLEDBY(), VALUE('x'))

    @special('compile/pick-test', patterns=pat, unpack='match')
    def handler(compiler, caps):
        """
        Pick test
        """

        return None

    return handler


def _make_function():
    code, ns = build_expr([
        ['CONST', 1],
        ['RETURN', 1],
    ])
    return make_callable(code, ns, FormalsSpec())


class TestPickDocumenter(unittest.TestCase):

    def _registry(self):
        registry = MagicMock()
        registry.documenters = {
            'function': FunctionDocumenter,
            'kosyntax': KosSyntaxDocumenter,
        }
        return registry


    def _parent(self):
        bridge = MagicMock()
        bridge.env.config = MagicMock()
        return ModuleDocumenter(bridge, 'koskari.builtins')


    def test_kos_function_beats_stock_function(self):
        fn = _make_function()
        parent = self._parent()
        doccls = pick_documenter(fn, 'fn', False, parent, self._registry())
        self.assertIs(doccls, KosFunctionDocumenter)


    def test_stock_function_documents_car(self):
        import koskari.builtins as builtins

        car = builtins.car
        parent = self._parent()
        doccls = pick_documenter(car, 'car', False, parent, self._registry())
        self.assertIs(doccls, FunctionDocumenter)


    def test_kos_syntax_documenter_selected(self):
        syn = _make_syntax()
        parent = self._parent()
        doccls = pick_documenter(syn, 'handler', False, parent, self._registry())
        self.assertIs(doccls, KosSyntaxDocumenter)


class TestKosBuiltinsDirective(unittest.TestCase):

    def test_directive_registered_name(self):
        self.assertEqual(KosBuiltinsDirective.__name__, 'KosBuiltinsDirective')


# The end.
