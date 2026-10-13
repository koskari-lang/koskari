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
Full-stack inspect.signature validation over compiled functions.

Frameworks such as FastAPI drive request validation and type conversion
entirely from inspect.signature: they read each parameter's annotation
and default from the Signature object and convert incoming values with
the real types found there. These tests compile Koskari source and
assert that the resulting callables expose that exact surface.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import inspect

import pytest

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class TestInspectCompiledLambda(SpecialsTestCase):

    async def test_signature_param_and_return_types(self):
        src = "(lambda :: int [(a :: int) b] a)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        sig = inspect.signature(fn)
        self.assertEqual(list(sig.parameters.keys()), ['a', 'b'])
        self.assertIs(sig.parameters['a'].annotation, int)
        self.assertIs(sig.parameters['b'].annotation,
                      inspect.Parameter.empty)
        self.assertIs(sig.return_annotation, int)

    async def test_signature_annotation_with_default(self):
        src = "(lambda [(a :: int value: 9) b] a)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        sig = inspect.signature(fn)
        param = sig.parameters['a']
        self.assertIs(param.annotation, int)
        self.assertEqual(param.default, 9)
        self.assertIs(sig.parameters['b'].default,
                      inspect.Parameter.empty)

    async def test_signature_annotation_default_none(self):
        # value: None must register an optional default of None, not be
        # dropped as though no default were given (which made the parameter
        # required). None had doubled as the "no default" sentinel.
        src = "(lambda [(a :: int value: None) b] a)"
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        sig = inspect.signature(fn)
        param = sig.parameters['a']
        self.assertIs(param.annotation, int)
        self.assertIsNone(param.default)
        self.assertIsNot(param.default, inspect.Parameter.empty)
        self.assertIs(sig.parameters['b'].default,
                      inspect.Parameter.empty)

    async def test_signature_string_forward_ref_resolved(self):
        src = '(lambda [(a :: "int")] a)'
        expr, mod = self.compile_expr(src, int=int)
        fn = await expr()
        sig = inspect.signature(fn)
        self.assertIs(sig.parameters['a'].annotation, int)

    async def test_signature_matches_dunder_annotations(self):
        src = "(lambda :: str [(a :: int)] a)"
        expr, mod = self.compile_expr(src, int=int, str=str)
        fn = await expr()
        sig = inspect.signature(fn)
        self.assertIs(sig.parameters['a'].annotation,
                      fn.__annotations__['a'])
        self.assertIs(sig.return_annotation,
                      fn.__annotations__['return'])


class TestInspectCompiledDefine(SpecialsTestCase):

    async def test_define_function_signature_types(self):
        src_str = """
        (begin
          (define f (function f :: int [(x :: int)] (mul x 2)))
          f)
        """
        expr, mod = self.compile_expr(
            src_str, mul=lambda a, b: a * b, int=int)
        f = await expr()
        sig = inspect.signature(f)
        self.assertIs(sig.parameters['x'].annotation, int)
        self.assertIs(sig.return_annotation, int)


class TestInspectDrivenConversion(SpecialsTestCase):
    """
    The FastAPI pattern: read types from inspect.signature and use them
    to convert raw string inputs before calling the function
    """

    async def test_convert_args_from_signature_types(self):
        src = "(lambda [(a :: int) (b :: float)] (mul a b))"
        expr, mod = self.compile_expr(
            src, mul=lambda a, b: a * b, int=int, float=float)
        fn = await expr()

        raw = {'a': '6', 'b': '7.5'}
        converted = {}
        for name, param in inspect.signature(fn).parameters.items():
            self.assertIsNot(param.annotation, inspect.Parameter.empty)
            converted[name] = param.annotation(raw[name])

        self.assertEqual(converted, {'a': 6, 'b': 7.5})
        result = await fn(**converted)
        self.assertEqual(result, 45.0)

    async def test_default_used_when_input_missing(self):
        src = "(lambda [(a :: int) (b :: int value: 10)] (mul a b))"
        expr, mod = self.compile_expr(
            src, mul=lambda a, b: a * b, int=int)
        fn = await expr()

        raw = {'a': '4'}
        converted = {}
        for name, param in inspect.signature(fn).parameters.items():
            if name in raw:
                converted[name] = param.annotation(raw[name])
            else:
                self.assertIsNot(param.default, inspect.Parameter.empty)
                converted[name] = param.default

        self.assertEqual(converted, {'a': 4, 'b': 10})
        result = await fn(**converted)
        self.assertEqual(result, 40)


# The end.
