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
Hand-built forms carrying Mark atoms through the compiler: a marked
symbol binds and resolves under its mangled key, a marked keyword or
head is read as the atom it wraps, and quote strips marks

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest

from koskari.lib import Mark, gensym, keyword, symbol
from koskari.types import build_proper, is_symbol, unpack

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


def S(name):
    return symbol(name)


class MarkSpelling(object):
    """
    Template atoms spelled as Mark boxes
    """

    def M(self, name, mark=1):
        return Mark(symbol(name), mark)


    def MK(self, name, mark=1):
        return Mark(keyword(name), mark)


class GensymSpelling(object):
    """
    Template atoms spelled as gensyms; a keyword carries no marks
    """

    def M(self, name, mark=1):
        return gensym(symbol(name), marks=mark)


    def MK(self, name, mark=1):
        return keyword(name)


class TestMarkedForms(MarkSpelling, SpecialsTestCase):


    def compile_form(self, form, **defns):
        from koskari.compile import Compiler
        from koskari.lib import Module

        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        from . import _BUILTIN_REGISTRY, _register_specials
        _register_specials(vars(module), _BUILTIN_REGISTRY)
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.full_compile(form)
        return fragment.as_expression(mod_ns), module


    async def test_marked_define_binds_mangled_key(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), self.M("t"), 5),
            self.M("t"))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 5)
        self.assertIn("t#1", vars(module))
        self.assertNotIn("t", vars(module))


    async def test_marked_reference_does_not_see_bare_binding(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), S("t"), 1),
            build_proper(S("define"), self.M("t"), 2),
            build_proper(S("cons"), S("t"), self.M("t"), S("nil")))
        expr, module = self.compile_form(form)
        result = await expr()
        self.assertEqual(list(unpack(result)), [1, 2])


    async def test_marked_lambda_formal(self):
        form = build_proper(
            build_proper(S("lambda"), build_proper(self.M("x")), self.M("x")),
            3)
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 3)


    async def test_marked_keyword_formal_declares_mangled_name(self):
        fn = build_proper(
            S("lambda"), build_proper(self.MK("argv"), 9), self.M("argv"))
        expr, module = self.compile_form(build_proper(fn))
        self.assertEqual(await expr(), 9)


    async def test_marked_let_binding(self):
        form = build_proper(
            S("let"),
            build_proper(build_proper(self.M("t"), 4)),
            build_proper(S("add"), self.M("t"), 1))
        expr, module = self.compile_form(form, add=lambda a, b: a + b)
        self.assertEqual(await expr(), 5)


    async def test_marked_setq(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), self.M("t"), 1),
            build_proper(S("setq"), self.M("t"), 2),
            self.M("t"))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 2)


    async def test_marked_head_resolves_special(self):
        form = build_proper(self.M("cond"), build_proper(True, 1))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 1)


    async def test_marked_literal_symbol(self):
        expr, module = self.compile_form(self.M("None"))
        self.assertIsNone(await expr())


    async def test_marked_keyword_label_at_call_site(self):
        form = build_proper(S("f"), self.MK("x"), 1)
        expr, module = self.compile_form(form, f=lambda x: x + 10)
        self.assertEqual(await expr(), 11)


    async def test_marked_else_tag_in_cond(self):
        form = build_proper(
            S("cond"),
            build_proper(False, 1),
            build_proper(self.MK("else"), 2))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 2)


    async def test_marked_keyword_as_value_is_the_keyword(self):
        expr, module = self.compile_form(self.MK("k"))
        self.assertIs(await expr(), keyword("k"))


    async def test_quote_strips_marks(self):
        form = build_proper(self.M("quote"), build_proper(self.M("t"), self.M("u")))
        expr, module = self.compile_form(form)
        items = list(unpack(await expr()))
        self.assertEqual(items, [S("t"), S("u")])
        self.assertTrue(all(is_symbol(i) for i in items))


    async def test_quote_atom_strips_mark(self):
        form = build_proper(S("quote"), self.M("t"))
        expr, module = self.compile_form(form)
        self.assertIs(await expr(), S("t"))


    async def test_quasiquote_strips_literal_marks_and_reads_unquote(self):
        form = build_proper(
            self.M("quasiquote"),
            build_proper(
                self.M("a"),
                build_proper(self.M("unquote"), build_proper(S("add"), 1, 1))))
        expr, module = self.compile_form(form, add=lambda a, b: a + b)
        self.assertEqual(list(unpack(await expr())), [S("a"), 2])


    async def test_attribute_name_is_a_label(self):
        class Obj:
            name = "n"

        form = build_proper(S("attr"), S("o"), self.M("name"))
        expr, module = self.compile_form(form, o=Obj())
        self.assertEqual(await expr(), "n")


    async def test_dot_call_attribute_is_a_label(self):
        form = build_proper(self.M(".upper"), "a")
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), "A")


class TestMarkResolution(TestMarkedForms):
    """
    A marked reference resolves to its key when something visible
    declares it, and falls back through its shorter spellings to the
    bare name otherwise
    """


    async def test_free_reference_resolves_to_bare_global(self):
        form = build_proper(self.M("add"), 1, 2)
        expr, module = self.compile_form(form, add=lambda a, b: a + b)
        self.assertEqual(await expr(), 3)


    async def test_free_reference_inside_lambda(self):
        fn = build_proper(
            S("lambda"), build_proper(self.M("x")),
            build_proper(self.M("add"), self.M("x"), 1))
        expr, module = self.compile_form(
            build_proper(fn, 2), add=lambda a, b: a + b)
        self.assertEqual(await expr(), 3)


    async def test_marked_reference_falls_back_to_bare_local(self):
        form = build_proper(
            S("let"), build_proper(build_proper(S("t"), 1)), self.M("t"))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 1)


    async def test_closure_over_inline_scope_marked_binding(self):
        form = build_proper(
            S("let"), build_proper(build_proper(self.M("t"), 5)),
            build_proper(build_proper(S("lambda"), build_proper(), self.M("t"))))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 5)


    async def test_nested_inline_scopes(self):
        form = build_proper(
            S("let"), build_proper(build_proper(self.M("t"), 5)),
            build_proper(
                S("let"), build_proper(build_proper(S("u"), 1)),
                build_proper(self.M("add"), self.M("t"), S("u"))))
        expr, module = self.compile_form(form, add=lambda a, b: a + b)
        self.assertEqual(await expr(), 6)


    async def test_doubly_marked_reference_finds_singly_marked_binder(self):
        form = build_proper(
            S("let"), build_proper(build_proper(self.M("t"), 5)),
            self.M("t", (1, 2)))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 5)


    async def test_setq_to_free_marked_name(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), S("c"), 1),
            build_proper(S("setq"), self.M("c"), 2),
            S("c"))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 2)


    async def test_definition_before_closure_reference(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), self.M("h"), build_proper(
                S("lambda"), build_proper(), 42)),
            build_proper(S("define"), self.M("g"), build_proper(
                S("lambda"), build_proper(), build_proper(self.M("h")))),
            build_proper(self.M("g")))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 42)


    async def test_closure_reference_before_definition_resolves(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), self.M("g"), build_proper(
                S("lambda"), build_proper(), build_proper(self.M("h")))),
            build_proper(S("define"), self.M("h"), build_proper(
                S("lambda"), build_proper(), 42)),
            build_proper(self.M("g")))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 42)


    async def test_free_reference_ignores_sibling_scope_binder(self):
        form = build_proper(
            S("begin"),
            build_proper(S("let"), build_proper(build_proper(self.M("t"), 1)), self.M("t")),
            self.M("t"))
        expr, module = self.compile_form(form, t=100)
        self.assertEqual(await expr(), 100)


class TestMarkResolutionStreams(TestMarkedForms):
    """
    The resolution pass walks the compiler's own emission: a class
    body's PUSH_COLLECT carries its namespace spec as the operand, and
    a comparison the processor fuses later is still plain loads here
    """


    async def test_marked_reference_inside_class_body(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), self.M("base"), 4),
            build_proper(S("define"), self.M("C"), build_proper(
                S("class"), self.M("C"), build_proper(),
                build_proper(S("define"), self.M("member"), self.M("base")))),
            build_proper(S("attr"), self.M("C"), S("member")))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), 4)


    async def test_marked_operands_of_a_fused_comparison(self):
        form = build_proper(
            S("begin"),
            build_proper(S("define"), self.M("t"), 3),
            build_proper(
                S("cond"),
                build_proper(build_proper(S("<"), self.M("t"), 5), "lt"),
                build_proper(self.MK("else"), "ge")))
        expr, module = self.compile_form(form)
        self.assertEqual(await expr(), "lt")


class TestGensymForms(GensymSpelling, TestMarkedForms):
    """
    The same forms with the template atoms spelled as gensyms
    """


class TestGensymResolution(GensymSpelling, TestMarkResolution):
    pass


class TestGensymResolutionStreams(GensymSpelling, TestMarkResolutionStreams):
    pass


# The end.
