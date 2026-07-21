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
The stray_colon compiler flag: allow / warn / error on a bare ``:``
symbol compiled as a name.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest
import warnings

from koskari.bootstrap import make_bootstrap
from koskari.compile import Compiler
from koskari.compile.pragma import StrayColon
from koskari.exceptions import CompilerError
from koskari.lib import Module, ModuleNamespace
from koskari.reader import Reader, source_str

import pytest

pytestmark = pytest.mark.phase_compile


class TestStrayColonLint(unittest.TestCase):
    """stray_colon gates compiling a bare : symbol as a name"""

    def compile_src(self, src, stray_colon):
        mod_ns = ModuleNamespace("test", dict(make_bootstrap()))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__,
                            stray_colon=stray_colon)
        source = Reader().read(source_str(src, "<unittest>"))
        return compiler.full_compile(source)

    def test_allow_compiles(self):
        """ALLOW: a stray : compiles as an ordinary name lookup"""

        self.compile_src("(foo bar : baz)", StrayColon.ALLOW)

    def test_error_raises(self):
        """ERROR: a stray : raises a CompilerError"""

        with self.assertRaises(CompilerError):
            self.compile_src("(foo bar : baz)", StrayColon.ERROR)

    def test_warn_warns_but_compiles(self):
        """WARN: a stray : warns, attributed to the source, not compiler.py"""

        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src("(foo bar : baz)", StrayColon.WARN)
        # attributed to the koskari source location, not compiler internals
        self.assertFalse(cm.filename.endswith("compiler.py"))

    def test_warn_names_the_enclosing_form_line(self):
        """WARN: the colon is interned, so it borrows the form's line"""

        # the innermost enclosing form is the for-each on line 2, not the
        # begin that opens on line 1
        src = "(begin\n  (for-each [foo (foos)]: foo))"
        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src(src, StrayColon.WARN)
        self.assertEqual(2, cm.lineno)

    def test_warn_reports_each_colon_once(self):
        """WARN: one diagnostic per stray colon, head position included"""

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            self.compile_src("(: foo)", StrayColon.WARN)
        self.assertEqual(1, len(caught))

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            self.compile_src("(foo : bar :)", StrayColon.WARN)
        self.assertEqual(2, len(caught))

    def test_half_converted_layout_flagged(self):
        """the motivating case: a layout block colon left in an s-expr"""

        # (for-each [x (xs)]: body) reads as (for-each [x (xs)] : body)
        with self.assertRaises(CompilerError):
            self.compile_src("(for-each [x (xs)]: body)", StrayColon.ERROR)

    def test_keyword_never_flagged(self):
        """foo: is a keyword, not a stray colon symbol"""

        self.compile_src("(foo bar: baz)", StrayColon.ERROR)

    def test_colon_in_head_position_flagged(self):
        """a : reaching the callable slot is diagnosed like any other"""

        with self.assertRaises(CompilerError):
            self.compile_src("(: foo)", StrayColon.ERROR)

    def test_quoted_colon_is_immune(self):
        """a quoted : is data, never compiled as a name"""

        self.compile_src("'(foo : bar)", StrayColon.ERROR)

    def test_string_flag_value_coerced(self):
        """a string flag value behaves like the matching enum member"""

        with self.assertRaises(CompilerError):
            self.compile_src("(foo bar : baz)", "error")

    def test_property_coerces_and_identity_checks(self):
        """the stray_colon property coerces str/member and rejects garbage"""

        module = Module(ModuleNamespace("test", {}))
        c = Compiler("<unittest>", module, stray_colon="warn")
        self.assertIs(c.stray_colon, StrayColon.WARN)

        c.stray_colon = "error"
        self.assertIs(c.stray_colon, StrayColon.ERROR)

        c.stray_colon = StrayColon.ALLOW
        self.assertIs(c.stray_colon, StrayColon.ALLOW)

        with self.assertRaises(ValueError):
            c.stray_colon = "bogus"

    def test_pragma_baseline_reaches_compiler(self):
        """an undeclared compiler/stray-colon loads the WARN baseline"""

        module = Module(ModuleNamespace("test", {}))
        c = Compiler.from_pragma("<unittest>", module)
        self.assertIs(c.stray_colon, StrayColon.WARN)


# The end.
