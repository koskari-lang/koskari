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
tests.lexer.lsp.test_handlers

Unit tests for koskari.lexer.lsp handlers.
"""


import pytest

pytest.importorskip('pygls')

from unittest import TestCase

from lsprotocol.types import CompletionParams, HoverParams, Position

from koskari.lexer.lsp.handlers import (
    collect_diagnostics,
    handle_completion,
    handle_hover,
)


class _FakeDoc:
    def __init__(self, source, uri='file:///test.ki'):
        self.source = source
        self.uri = uri
        self.lines = source.splitlines() if source else []
        self.version = 1


class _FakeWorkspace:
    def get_text_document(self, uri):
        return self._docs.get(uri)


class _FakeLS:
    def __init__(self):
        self.diagnostics = {}
        self.workspace = _FakeWorkspace()
        self.workspace._docs = {}


class TestCollectDiagnostics(TestCase):

    def test_valid_source_no_diagnostics(self):
        diags = collect_diagnostics('(+ 1 2)', 'file:///test.ki')
        self.assertEqual(diags, [])


    def test_invalid_source_emits_diagnostic(self):
        diags = collect_diagnostics('(unclosed', 'file:///test.ki')
        self.assertGreater(len(diags), 0)
        self.assertIn('eof', diags[0].message.lower())


    def test_layout_error_emits_diagnostic(self):
        diags = collect_diagnostics(
            'define foo []:\n  if:\n    x', 'file:///test.ki')
        self.assertGreater(len(diags), 0)


class TestHandleCompletion(TestCase):

    def test_completion_returns_items_for_prefix(self):
        ls = _FakeLS()
        doc = _FakeDoc('if')
        ls.workspace._docs = {doc.uri: doc}
        params = CompletionParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=2))
        result = handle_completion(ls, params)
        labels = [item.label for item in result.items]
        self.assertIn('if', labels)


    def test_completion_includes_layout_heads(self):
        ls = _FakeLS()
        doc = _FakeDoc('if ')
        ls.workspace._docs = {doc.uri: doc}
        params = CompletionParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=2))
        result = handle_completion(ls, params)
        labels = [item.label for item in result.items]
        self.assertIn('if', labels)


    def test_completion_filters_by_prefix(self):
        ls = _FakeLS()
        doc = _FakeDoc('def')
        ls.workspace._docs = {doc.uri: doc}
        params = CompletionParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=3))
        result = handle_completion(ls, params)
        for item in result.items:
            self.assertTrue(
                item.label.startswith('def'),
                msg='Completion %r should start with def' % item.label)


class TestHandleHover(TestCase):

    def test_hover_on_layout_returns_content(self):
        ls = _FakeLS()
        doc = _FakeDoc('if True:\n  1')
        ls.workspace._docs = {doc.uri: doc}
        params = HoverParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=1))
        result = handle_hover(ls, params)
        self.assertIsNotNone(result)
        self.assertIn('if', result.contents.value)


    def test_hover_on_if_returns_content(self):
        ls = _FakeLS()
        doc = _FakeDoc('if True:\n  1')
        ls.workspace._docs = {doc.uri: doc}
        params = HoverParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=1))
        result = handle_hover(ls, params)
        self.assertIsNotNone(result)
        self.assertIn('if', result.contents.value)


# The end.
