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
tests.reproducers.test_mark_hygiene_forms

Hygiene by marks across the binding forms: a name a template binds
through each special form is the template's own, so a use-site global
spelled the same is neither read nor written by the expansion

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


async def load(name, src):
    module = new_module(name)
    vars(module)["__file__"] = f"<{name}>"
    await load_module_source_async(module, src)
    return vars(module)


def spell(value):
    """
    A use-site value as Koskari source: a str in double quotes, any
    other value as Python spells it
    """

    return f'"{value}"' if isinstance(value, str) else repr(value)


class TestTemplateBindersAcrossForms(unittest.IsolatedAsyncioTestCase):
    """
    Each case defines a macro whose template binds a name through one
    form, defines a use-site global under the same spelling, expands
    the macro once, and checks both the expansion's result and the
    untouched global
    """

    async def check(self, name, body, expected, prelude="", **shadows):
        defines = "\n".join(
            f"(var {sym} {spell(val)})" for sym, val in shadows.items())
        src = (
            f"{prelude}\n"
            f"(define-syntax m (syntax-rules [] [(m) {body}]))\n"
            f"{defines}\n"
            "(var answer (m))\n")
        ns = await load(f"mhf_{name}", src)
        self.assertEqual(ns["answer"], expected)
        for sym, val in shadows.items():
            self.assertEqual(ns[sym], val)


    async def test_try_except_binder(self):
        await self.check(
            "try",
            '(try (raise (ValueError "boom")) [[e Exception] (str e)])',
            "boom", e=1000)


    async def test_with_binder(self):
        await self.check(
            "with",
            '(with [c (StringIO "hi")] (c.read))',
            "hi", prelude="(import-from io StringIO)", c=1000)


    async def test_for_each_variable(self):
        await self.check(
            "for_each",
            "(begin (var acc 0)"
            " (for-each [i #[1 2 3]] (setq acc (+ acc i))) acc)",
            6, i=1000, acc=1000)


    async def test_while_variable(self):
        await self.check(
            "while",
            "(begin (var k 3) (var acc 0)"
            " (while (> k 0) (setq acc (+ acc k)) (setq k (- k 1))) acc)",
            6, k=999, acc=999)


    async def test_let_star(self):
        await self.check(
            "let_star",
            "(let* [[a 1] [b (+ a 1)]] (+ a b))",
            3, a=100, b=100)


    async def test_letrec(self):
        await self.check(
            "letrec",
            "(letrec [[f (lambda [n] (if (== n 0) 1 (* n (f (- n 1)))))]]"
            " (f 4))",
            24, f=100, n=100)


    async def test_rest_formals(self):
        await self.check(
            "rest",
            "(begin (def f [*: args **: kw] (+ (len args) (len kw)))"
            " (f 1 2 x: 3))",
            3, args=999, kw=999)


    async def test_decorator(self):
        await self.check(
            "decorator",
            "(begin (def wrap [f] (lambda [] (+ (f) 1)))"
            " (decorate [wrap] (def f [] 5)) (f))",
            6, wrap=55, f=55)


    async def test_keyword_arguments_at_a_call(self):
        await self.check(
            "kwargs",
            "(begin (def f [a: 0 b: 0] (+ a b)) (f a: 1 b: 2))",
            3, a=100, b=100)


    async def test_let_cc_binder(self):
        await self.check("let_cc", "(let/cc k (k 5) 99)", 5, k=1000)


    async def test_shift_binder(self):
        await self.check(
            "shift", "(reset (+ 1 (shift kk (kk 4))))", 5, kk=1000)


    async def test_refq(self):
        await self.check(
            "refq",
            "(begin (var t 0) (var r (refq t)) (r.store 9) t)",
            9, t=100, r=100)


    async def test_takeq_and_delq(self):
        await self.check(
            "takeq",
            "(begin (var t 3) (var u 4) (delq u) (takeq t))",
            3, t=100, u=100)


    async def test_define_values(self):
        await self.check(
            "define_values",
            "(begin (var [a b] #(1 2)) (+ a b))",
            3, a=100, b=200)


    async def test_define_environ(self):
        await self.check(
            "define_environ",
            '(begin (define-environ KOS_MH_UNSET "dflt") KOS_MH_UNSET)',
            "dflt", KOS_MH_UNSET="mine")


    async def test_generator_body(self):
        await self.check(
            "generator",
            "(begin (def gen [] (for-each [x #[1 2 3]] (yield (* x 2))))"
            " (var out #[]) (for-each [v (gen)] (out.append v)) out)",
            [2, 4, 6], prelude="(import-from asyncio sleep)",
            x=999, v=999, out=999)


    async def test_async_generator_body(self):
        await self.check(
            "async_generator",
            "(begin (def gen [] (for-each [x #[1 2 3]]"
            " (sleep 0) (yield (* x 2))))"
            " (var out #[]) (for-each [v (gen)] (out.append v)) out)",
            [2, 4, 6], prelude="(import-from asyncio sleep)",
            x=999, v=999, out=999)


    async def test_class_name(self):
        await self.check(
            "class",
            "(begin (class K [] (var v 5)) K.v)",
            5, K=1000)


    async def test_global_declaration(self):
        await self.check("global", "(begin (global var t 4) t)", 4, t=100)


# The end.
