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
tests.extras.pygments.test_pygments_lexer

Unit tests for optional koskari.extras.pygments integration.
"""


import importlib
import sys
import unittest

import pytest


pytestmark = pytest.mark.phase_extras


class TestPygmentsOptional(unittest.TestCase):

    def test_import_koskari_lexer_without_pygments_submodule(self):
        import koskari.lexer
        self.assertTrue(hasattr(koskari.lexer, 'tokenize'))


    def test_pygments_subpackage_import_without_pygments_installed(self):
        if 'pygments' in sys.modules:
            raise unittest.SkipTest('pygments already loaded in this process')
        mod = importlib.import_module('koskari.extras.pygments')
        self.assertTrue(hasattr(mod, 'register_sphinx_lexer'))


class TestPygmentsLexer(unittest.TestCase):

    def setUp(self):
        pytest.importorskip('pygments')
        from koskari.extras.pygments import KoskariLexer
        self.KoskariLexer = KoskariLexer


    def test_lexer_covers_full_source(self):
        source = '(define x 1)'
        lexer = self.KoskariLexer()
        spans = list(lexer.get_tokens_unprocessed(source))
        covered = sum(len(value) for _idx, _tok, value in spans)
        self.assertEqual(covered, len(source))


    def test_lexer_preserves_whitespace_gaps(self):
        source = '(+ 1 2)'
        lexer = self.KoskariLexer()
        from pygments.token import Text
        spans = list(lexer.get_tokens_unprocessed(source))
        gap_text = ''.join(
            value for _idx, tok, value in spans if tok is Text)
        self.assertIn(' ', gap_text)


    def test_lexer_marks_surface_specials(self):
        from pygments.token import Name
        lexer = self.KoskariLexer()
        spans = list(lexer.get_tokens_unprocessed('(define x 1)'))
        define_tok = [tok for _idx, tok, val in spans if val == 'define'][0]
        self.assertEqual(define_tok, Name.Builtin)


    def test_lexer_marks_import_as_special(self):
        from pygments.token import Name
        lexer = self.KoskariLexer()
        spans = list(lexer.get_tokens_unprocessed('(import os)'))
        import_tok = [tok for _idx, tok, val in spans if val == 'import'][0]
        self.assertEqual(import_tok, Name.Builtin)


    def test_highlight_define(self):
        from koskari.extras.pygments import highlight_source
        html = highlight_source('(define x 1)')
        self.assertIn('define', html)


    def test_highlight_collections(self):
        from koskari.extras.pygments import highlight_source
        for source in ('#[1 2 3]', '#{(a 1)}', '(import os)'):
            html = highlight_source(source)
            self.assertIn('<span', html)


    def test_highlight_layout_head(self):
        from koskari.extras.pygments import highlight_source
        html = highlight_source('if:\n  (print 1)')
        self.assertIn('if', html)


# The end.
