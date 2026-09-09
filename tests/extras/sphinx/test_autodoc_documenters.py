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
tests.extras.sphinx.test_autodoc_documenters

Koskari Sphinx autodocumenter selection and objtype
"""

import unittest
from unittest.mock import MagicMock

import pytest


pytest.importorskip('sphinx')

from sphinx.ext.autodoc import (
    DataDocumenter, FunctionDocumenter, ModuleDocumenter)

from koskari.extras.sphinx.autodoc import (
    KosFunctionDocumenter, KosLayoutDocumenter, KosSyntaxDocumenter,
    pick_documenter)
from koskari.lib import FormalsSpec
from koskari.matcher import CALLEDBY, MATCH, VALUE
from koskari.reader.layouts import layout_if
from koskari.reader.layouts.helpers import layout
from koskari.syntax import special
from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_extras


def _make_syntax():
    pat = MATCH(CALLEDBY(), VALUE('x'))

    @special('compile/foo', patterns=pat, unpack='match')
    def handler(compiler, caps):
        """
        Doc for compile/foo
        """

        return None

    return handler


def _make_layout():
    @layout('layout/test', peers=('layout/peer',))
    def read_layout(source, peers):
        """
        Layout doc
        """

        return None

    return read_layout


def _make_function():
    code, ns = build_expr([
        ['CONST', 1],
        ['RETURN', 1],
    ])
    return make_callable(code, ns, FormalsSpec())


class TestAutodocDocumenters(unittest.TestCase):

    def test_kos_function_documenter_matches_function(self):
        fn = _make_function()
        parent = None
        self.assertTrue(KosFunctionDocumenter.can_document_member(
            fn, 'attr', True, parent))
        self.assertFalse(FunctionDocumenter.can_document_member(
            fn, 'attr', True, parent))


    def test_pick_documenter_prefers_kos_function(self):
        fn = _make_function()
        bridge = self._documenter_bridge()
        bridge.env = MagicMock()
        bridge.env._registry = MagicMock()
        bridge.env._registry.documenters = {
            'function': FunctionDocumenter,
            'kosyntax': KosSyntaxDocumenter,
        }
        parent = ModuleDocumenter(bridge, 'koskari.builtins')
        doccls = pick_documenter(
            fn, 'fn', False, parent, bridge.env._registry)
        self.assertIs(doccls, KosFunctionDocumenter)


    def test_kos_syntax_documenter_matches_syntax(self):
        syn = _make_syntax()
        parent = None
        self.assertTrue(KosSyntaxDocumenter.can_document_member(
            syn, 'handler', True, parent))
        self.assertEqual(KosSyntaxDocumenter.objtype, 'kosyntax')


    def test_kos_layout_documenter_matches_layout_handler(self):
        handler = _make_layout()
        parent = None
        self.assertTrue(KosLayoutDocumenter.can_document_member(
            handler, 'read_layout', True, parent))
        self.assertEqual(KosLayoutDocumenter.objtype, 'kolayout')


    def test_documenters_outrank_data_documenter(self):
        self.assertGreater(KosFunctionDocumenter.priority, DataDocumenter.priority)
        self.assertGreater(KosSyntaxDocumenter.priority, DataDocumenter.priority)
        self.assertGreater(KosLayoutDocumenter.priority, DataDocumenter.priority)


    def _documenter_bridge(self):
        bridge = MagicMock()
        bridge.env.config = MagicMock()
        return bridge


    def test_syntax_format_name_prefers_symbol(self):
        syn = _make_syntax()
        doc = KosSyntaxDocumenter(self._documenter_bridge(), 'handler')
        doc.object = syn
        doc.objpath = ['handler']
        self.assertEqual(doc.format_name(), 'compile/foo')


    def test_layout_format_name_prefers_head(self):
        handler = layout_if
        doc = KosLayoutDocumenter(self._documenter_bridge(), 'layout_if')
        doc.object = handler
        doc.objpath = ['layout_if']
        self.assertEqual(doc.format_name(), str(handler.head))


# The end.
