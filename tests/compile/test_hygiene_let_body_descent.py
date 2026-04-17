# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.

"""
Hygiene discovery must descend into the ``compile/let`` body when the body is
a single nested form.

``REST`` body extraction wraps a lone body form as ``(FORM . nil)``. The old
``collect_binding_symbols`` logic only walked ``cdr`` siblings, so the nested
``(compile/lambda [y] ...)`` under a ``compile/let`` never contributed lambda
formals to the binding set—a procedural macro could not hygienically freshen
``y`` alongside ``let`` names.

:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.compile import Compiler
from koskari.compile.specials import SPECIALS
from koskari.hygiene import collect_binding_symbols, expand_template_with_hygiene
from koskari.lib import Module, ModuleNamespace, car, cdr, symbol
from koskari.reader import default_reader, source_str
from koskari.types import is_pair, is_symbol


pytestmark = pytest.mark.phase_compile


def _register_specials(ns, specials):
    for sym, fn in specials.items():
        ns.store(str(sym), fn)


def _make_compiler():
    mod_ns = ModuleNamespace("test", {})
    _register_specials(mod_ns, SPECIALS)
    module = Module(mod_ns)
    return Compiler("<test>", module, filename=__file__), mod_ns


class TestHygieneLetBodyDescent(unittest.TestCase):
    """
    Reproducer: ``collect_binding_symbols`` / ``expand_template_with_hygiene``
    on a parsed ``compile/let`` whose body is one ``compile/lambda``.
    """

    def test_collect_standalone_lambda_only_lambda_formals(self):
        """Baseline: hygiene sees ``lambda`` formals without any enclosing ``let``."""

        src = "(compile/lambda [y] (+ y))"
        form = default_reader.read(source_str(src, __file__, line_no=1))

        compiler, _mod_ns = _make_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)

        self.assertSetEqual({str(s) for s in syms}, {"y"})

    def test_collect_two_let_body_forms_still_include_nested_lambda_formal(self):
        """
        With several body forms, nested specials are reached via the tail walk
        (not only the ``(FORM . nil)`` lone-body path); binders must still match.
        """

        src = "(compile/let [[t 1]] 0 (compile/lambda [y] (+ t y)))"
        form = default_reader.read(source_str(src, __file__, line_no=1))

        compiler, _mod_ns = _make_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)

        self.assertSetEqual({str(s) for s in syms}, {"t", "y"})

    def test_collect_includes_let_binders_and_nested_lambda_formals(self):
        """
        Both ``t`` (let binding) and ``y`` (lambda formal) participate in
        hygiene; ``y`` must not be dropped when the lambda is the sole let body.
        """

        src = "(compile/let [[t 1]] (compile/lambda [y] (+ t y)))"
        form = default_reader.read(source_str(src, __file__, line_no=1))

        compiler, _mod_ns = _make_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)

        self.assertSetEqual({str(s) for s in syms}, {"t", "y"})

    def test_expand_renames_let_and_lambda_binders_consistently(self):
        """
        After expansion, ``t`` and ``y`` are gensymmed everywhere: binding RHS,
        lambda formals, and ``(+ ...)`` references.
        """

        src = "(compile/let [[t 1]] (compile/lambda [y] (+ t y)))"
        form = default_reader.read(source_str(src, __file__, line_no=1))

        compiler, _mod_ns = _make_compiler()
        syms = collect_binding_symbols(form, compiler.find_compiled)
        out = expand_template_with_hygiene(form, {}, syms, {})

        self.assertTrue(is_pair(out))
        self.assertEqual(car(out), symbol("compile/let"))

        bind_block, lam_tail = car(cdr(out)), cdr(cdr(out))
        bpair = car(bind_block)
        let_name = car(bpair)
        rhs = cdr(bpair)
        rhs_val = car(rhs) if is_pair(rhs) else rhs
        self.assertEqual(rhs_val, 1)
        self.assertTrue(is_symbol(let_name))
        self.assertNotEqual(str(let_name), "t")

        lam = car(lam_tail)
        self.assertEqual(car(lam), symbol("compile/lambda"))
        formals = car(cdr(lam))
        formal_y = car(formals)
        self.assertTrue(is_symbol(formal_y))
        self.assertNotEqual(str(formal_y), "y")

        body = car(cdr(cdr(lam)))
        self.assertEqual(car(body), symbol("+"))
        self.assertIs(car(cdr(body)), let_name)
        self.assertIs(car(cdr(cdr(body))), formal_y)
