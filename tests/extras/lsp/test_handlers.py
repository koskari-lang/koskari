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
tests.extras.lsp.test_handlers

Unit tests for koskari.extras.lsp handlers.
"""


from unittest import IsolatedAsyncioTestCase

import pytest
from lsprotocol.types import CompletionParams, HoverParams, Position

from koskari.extras.lsp.handlers import (
    collect_diagnostics, handle_completion, handle_hover)


pytestmark = pytest.mark.phase_extras


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


class TestCollectDiagnostics(IsolatedAsyncioTestCase):

    async def test_valid_source_no_diagnostics(self):
        diags = collect_diagnostics('(+ 1 2)', 'file:///test.ki')
        self.assertEqual(diags, [])


    async def test_invalid_source_emits_diagnostic(self):
        diags = collect_diagnostics('(unclosed', 'file:///test.ki')
        self.assertGreater(len(diags), 0)
        self.assertIn('eof', diags[0].message.lower())


    async def test_diagnostic_range_starts_on_the_faulting_character(self):
        """
        A reader location carries a 0-based column; the range starts on
        that character, not one to its left
        """

        diags = collect_diagnostics('(a "hello', 'file:///test.ki')
        start = diags[0].range.start
        self.assertEqual((start.line, start.character), (0, 3))
        self.assertEqual(diags[0].range.end.character, 4)

        diags = collect_diagnostics('(+ 1 2', 'file:///test.ki')
        start = diags[0].range.start
        self.assertEqual((start.line, start.character), (0, 0))


    async def test_layout_error_emits_diagnostic(self):
        diags = collect_diagnostics(
            'def foo []:\n  if:\n    x', 'file:///test.ki')
        self.assertGreater(len(diags), 0)


    async def test_layout_off_pragma_reports_no_diagnostic(self):
        """
        a file turning reader/layout off reads its later forms as
        s-expressions, so an indented form is not reported
        """

        src = (
            '(pragma [reader/layout False])\n'
            '(define f (lambda (x)\n'
            '  (+ x 1)))\n'
            '  (print "ok")\n')
        diags = collect_diagnostics(src, 'file:///test.ki')
        self.assertEqual(diags, [])


    async def test_implicit_calls_off_pragma_reports_bare_call_line(self):
        """
        a file turning reader/implicit-calls off gets a diagnostic on a
        line holding several forms
        """

        src = (
            '(pragma [reader/implicit-calls False])\n'
            'print 1 2\n')
        diags = collect_diagnostics(src, 'file:///test.ki')
        self.assertEqual(len(diags), 1)
        self.assertEqual(diags[0].range.start.line, 1)


class TestHandleCompletion(IsolatedAsyncioTestCase):

    async def test_completion_returns_items_for_prefix(self):
        ls = _FakeLS()
        doc = _FakeDoc('if')
        ls.workspace._docs = {doc.uri: doc}
        params = CompletionParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=2))
        result = handle_completion(ls, params)
        labels = [item.label for item in result.items]
        self.assertIn('if', labels)


    async def test_completion_includes_layout_heads(self):
        ls = _FakeLS()
        doc = _FakeDoc('if ')
        ls.workspace._docs = {doc.uri: doc}
        params = CompletionParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=2))
        result = handle_completion(ls, params)
        labels = [item.label for item in result.items]
        self.assertIn('if', labels)


    async def test_completion_filters_by_prefix(self):
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


class TestHandleHover(IsolatedAsyncioTestCase):

    async def test_hover_on_layout_returns_content(self):
        ls = _FakeLS()
        doc = _FakeDoc('if True:\n  1')
        ls.workspace._docs = {doc.uri: doc}
        params = HoverParams(
            text_document={'uri': doc.uri},
            position=Position(line=0, character=1))
        result = handle_hover(ls, params)
        self.assertIsNotNone(result)
        self.assertIn('if', result.contents.value)


    async def test_hover_on_if_returns_content(self):
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
