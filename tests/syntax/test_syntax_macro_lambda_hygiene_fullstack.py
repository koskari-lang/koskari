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
Full-stack hygiene proofs: syntax demonstration macros expand to
compile/lambda-ng forms parsed via LayoutReader; introduced formals are
renamed; runtime matches expanded lambda behavior.
"""


import pytest

from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile import Compiler
from koskari.lib import Datum, Module, car, cdr, cons, nil, symbol
from koskari.reader import IndentAwareStream, LayoutReader, source_str
from koskari.types import (
    build_proper, is_mark, is_pair, is_symbol, strip_mark)
from tests.compile.specials import _register_specials
from tests.syntax.datum_syntax_helpers import (
    macro_from_rule_rows, syntax_from_match_datums, syntax_from_rule_datums)

from .test_syntax_lambda_fullstack import LambdaNgFullStackBase


pytestmark = pytest.mark.phase_specials


# binding keys carry the compile/ prefix; patterns match the short
# spelling users write
SYM_DEMO_RULES = symbol("compile/syntax-ng-demo-rules")
SYM_DEMO_MATCH = symbol("compile/syntax-ng-demo-match")
SYM_DEMO_MACRO = symbol("compile/syntax-ng-demo-macro")
USE_DEMO_RULES = symbol("syntax-ng-demo-rules")
USE_DEMO_MATCH = symbol("syntax-ng-demo-match")

_LAMBDA_HEAD = symbol("lambda-ng")
_SRC_FORMAL = "n"


def _unwrap_datum(x):
    while isinstance(x, Datum):
        x = x.datum
    return x


def _install_lambda_ng_template(ns):
    reader = LayoutReader(ns)
    stream = IndentAwareStream.from_source(source_str(
        "(lambda-ng [n] (+ n 1))",
        __file__,
        line_no=1,
    ))
    return _unwrap_datum(reader.read(stream))


def _install_demo_syntaxes(ns, tpl):
    rules_syn = syntax_from_rule_datums(
        SYM_DEMO_RULES,
        (),
        ((build_proper(USE_DEMO_RULES), tpl),),
    )
    match_syn = syntax_from_match_datums(
        SYM_DEMO_MATCH,
        ((build_proper(USE_DEMO_MATCH), tpl),),
    )

    def macro_proc(compiler, **kw):
        return tpl

    macro_syn = macro_from_rule_rows(
        SYM_DEMO_MACRO,
        (),
        ((build_proper(SYM_DEMO_MACRO), macro_proc),),
    )
    ns[str(SYM_DEMO_RULES)] = rules_syn
    ns[str(SYM_DEMO_MATCH)] = match_syn
    ns[str(SYM_DEMO_MACRO)] = macro_syn
    return rules_syn, match_syn, macro_syn


def _make_compiler_and_ns(testcase):
    mod_ns = testcase.get_glbls()
    module = Module(mod_ns)
    ns = vars(module)
    _register_specials(ns, _BUILTIN_REGISTRY)
    tpl = _install_lambda_ng_template(ns)
    _install_demo_syntaxes(ns, tpl)
    compiler = Compiler("<unittest>", module, filename=__file__)
    return module, ns, compiler, tpl


def _read_form(ns, src_line):
    reader = LayoutReader(ns)
    stream = IndentAwareStream.from_source(
        source_str(src_line, __file__, line_no=1))
    return _unwrap_datum(reader.read(stream))


def assert_lambda_ng_formal_renamed(test, expanded, original_name):
    """
    Assert the first formal in an expanded compile/lambda-ng form was
    hygienically renamed away from original_name
    """

    exp = _unwrap_datum(expanded)
    test.assertTrue(is_pair(exp))
    test.assertEqual(strip_mark(car(exp)), _LAMBDA_HEAD)
    rest = cdr(exp)
    test.assertTrue(is_pair(rest))
    formals_cell = car(rest)
    if is_pair(formals_cell):
        param = car(formals_cell)
    else:
        param = formals_cell
    test.assertTrue(is_mark(param))
    test.assertTrue(is_symbol(param.datum))
    test.assertNotEqual(str(param), original_name)


class MacroLambdaHygieneFullStackBase(LambdaNgFullStackBase):


    def compile_expr_with_demos(self, src_str, lineno=1):
        """
        Like compile_expr but registers syntax demo macros and lambda-ng
        template in the module namespace before reading source.
        """

        mod_ns = self.get_glbls()
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)
        tpl = _install_lambda_ng_template(ns)
        _install_demo_syntaxes(ns, tpl)
        stream = IndentAwareStream.from_source(
            source_str(src_str, __file__, line_no=lineno))
        source = LayoutReader(ns).read(stream)
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        return expr, module


class TestNgDemoMacroHygieneStructure(MacroLambdaHygieneFullStackBase):


    def test_rules_demo_expansion_renames_lambda_formal(self):
        _mod, ns, compiler, _tpl = _make_compiler_and_ns(self)
        use_site = _read_form(ns, "(syntax-ng-demo-rules)")
        rules_syn = ns[str(SYM_DEMO_RULES)]
        expanded = rules_syn.compile_impl(compiler, use_site)
        assert_lambda_ng_formal_renamed(self, expanded, _SRC_FORMAL)


    def test_match_demo_expansion_renames_lambda_formal(self):
        _mod, ns, compiler, _tpl = _make_compiler_and_ns(self)
        use_site = _read_form(ns, "(syntax-ng-demo-match)")
        match_syn = ns[str(SYM_DEMO_MATCH)]
        expanded = match_syn.compile_impl(compiler, use_site)
        assert_lambda_ng_formal_renamed(self, expanded, _SRC_FORMAL)


    def test_macro_demo_expansion_renames_lambda_formal(self):
        _mod, ns, compiler, _tpl = _make_compiler_and_ns(self)
        use_site = _read_form(ns, "(syntax-ng-demo-macro)")
        macro_syn = ns[str(SYM_DEMO_MACRO)]
        expanded = macro_syn.compile_impl(compiler, use_site)
        assert_lambda_ng_formal_renamed(self, expanded, _SRC_FORMAL)


class TestNgDemoMacroHygieneRuntime(MacroLambdaHygieneFullStackBase):


    async def test_rules_demo_runtime_plus_one(self):
        src = (
            "(begin\n"
            "  (define F (syntax-ng-demo-rules))\n"
            "  (F 41))"
        )
        expr, _mod = self.compile_expr_with_demos(src)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_macro_demo_runtime_plus_one(self):
        src = (
            "(begin\n"
            "  (define G (syntax-ng-demo-macro))\n"
            "  (G 8))"
        )
        expr, _mod = self.compile_expr_with_demos(src)
        result = await expr()
        self.assertEqual(result, 9)


# The end.
