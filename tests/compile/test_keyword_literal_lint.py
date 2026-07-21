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
The keyword_literals compiler flag: allow / warn / error on an unquoted
keyword compiled as a literal value.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

from koskari.compile.pragma import KeywordLiterals
from koskari.exceptions import CompilerError
from koskari.lib import Module, ModuleNamespace
from koskari.module import build_module_load_facets
from koskari.reader import source_str

import pytest

pytestmark = pytest.mark.phase_compile


class TestKeywordLiteralLint(unittest.TestCase):
    """keyword_literals gates compiling a bare keyword as a value"""

    def compile_src(self, src, keyword_literals):
        # a fully loaded module: the macro layer has to be present, or
        # forms like (if ...) compile as plain calls and the keyword lands
        # in a keyword-argument slot rather than in value position
        import koskari.builtins as builtins

        module = Module(ModuleNamespace("test", dict(vars(builtins))))
        compiler, reader = build_module_load_facets(module, "<unittest>")
        compiler.keyword_literals = keyword_literals
        source = reader.read(source_str(src, "<unittest>"))
        return compiler.full_compile(source)

    def test_allow_compiles(self):
        """ALLOW: an unquoted keyword compiles to itself, no diagnostic"""

        self.compile_src("(if foo: 1 2)", KeywordLiterals.ALLOW)

    def test_error_raises(self):
        """ERROR: an unquoted keyword value raises a CompilerError"""

        with self.assertRaises(CompilerError):
            self.compile_src("(if foo: 1 2)", KeywordLiterals.ERROR)

    def test_warn_warns_but_compiles(self):
        """WARN: warns, attributed to the source, not compiler.py"""

        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src("(if foo: 1 2)", KeywordLiterals.WARN)
        self.assertFalse(cm.filename.endswith("compiler.py"))

    def test_message_names_the_keyword(self):
        """the diagnostic names the offending keyword and its quoted form"""

        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src("(if foo: 1 2)", KeywordLiterals.WARN)
        text = str(cm.warning)
        # the keyword keeps its colon, so the suggested form is one the
        # reader would actually accept
        self.assertIn("keyword foo:", text)
        self.assertIn("'foo:", text)

    def test_quoted_keyword_is_exempt(self):
        """'foo: states the intent and never reaches the check"""

        self.compile_src("(if 'foo: 1 2)", KeywordLiterals.ERROR)

    def test_quoted_list_of_keywords_is_exempt(self):
        """quoted list elements requote through the quote special"""

        self.compile_src("'(a foo: b)", KeywordLiterals.ERROR)

    def test_keyword_arguments_are_exempt(self):
        """a keyword argument is structural, not a value"""

        self.compile_src('(print 1 sep: "-")', KeywordLiterals.ERROR)

    def test_structural_marker_is_exempt(self):
        """cond consumes else: as a marker, so it is never a value"""

        self.compile_src("(cond [(> x 1) 'big] [else: 'small])",
                         KeywordLiterals.ERROR)

    def test_binding_value_flagged(self):
        """a keyword bound as a value is flagged like any other"""

        with self.assertRaises(CompilerError):
            self.compile_src("(let [[a foo:]] a)", KeywordLiterals.ERROR)

    def test_string_flag_value_coerced(self):
        """a string flag value behaves like the matching enum member"""

        with self.assertRaises(CompilerError):
            self.compile_src("(if foo: 1 2)", "error")

    def test_property_coerces_and_identity_checks(self):
        """the keyword_literals property coerces and rejects garbage"""

        from koskari.compile import Compiler

        module = Module(ModuleNamespace("test", {}))
        c = Compiler("<unittest>", module, keyword_literals="warn")
        self.assertIs(c.keyword_literals, KeywordLiterals.WARN)

        c.keyword_literals = "error"
        self.assertIs(c.keyword_literals, KeywordLiterals.ERROR)

        c.keyword_literals = KeywordLiterals.ALLOW
        self.assertIs(c.keyword_literals, KeywordLiterals.ALLOW)

        with self.assertRaises(ValueError):
            c.keyword_literals = "bogus"


# The end.
