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
Tests for inspect.Signature interop on KosFunction and KosBoundMethod.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import inspect
import unittest

import pytest

from koskari.lib import FormalsSpec, Function

from tests.lib.test_expression_function import build_expr

pytestmark = pytest.mark.phase_lib


class TestInspectKosFunction(unittest.TestCase):
    """
    inspect.signature, __signature__, __name__, __annotations__
    """

    def test_inspect_signature_unbound_lists_parameters(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        fn = code.function(fspec(), base_ns)
        sig = inspect.signature(fn)
        self.assertEqual(list(sig.parameters.keys()), ['x', 'y'])

    def test_inspect_signature_bind_positional(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        fn = code.function(fspec(), base_ns)
        sig = inspect.signature(fn)
        bound = sig.bind(1, 2)
        self.assertEqual(bound.arguments['x'], 1)
        self.assertEqual(bound.arguments['y'], 2)

    def test_dunder_signature_matches_inspect_signature(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), None, ())
        fn = code.function(fspec(), base_ns)
        self.assertEqual(fn.__signature__, inspect.signature(fn))

    def test_annotations_empty_dict(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ())
        fn = code.function(fspec(), base_ns)
        self.assertEqual(fn.__annotations__, {})

    def test_annotations_real_objects(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), None, ('b',))
        fn = Function(code, base_ns, fspec, {'a': int, 'b': str})
        self.assertEqual(fn.__annotations__, {'a': int, 'b': str})
        self.assertIs(fn.__annotations__['a'], int)

    def test_annotations_reflected_in_signature(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), None, ('b',))
        fn = Function(code, base_ns, fspec, {'a': int, 'return': str})
        sig = inspect.signature(fn)
        self.assertIs(sig.parameters['a'].annotation, int)
        self.assertIs(sig.parameters['b'].annotation,
                      inspect.Parameter.empty)
        self.assertIs(sig.return_annotation, str)

    def test_annotations_return_only(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ())
        fn = Function(code, base_ns, fspec, {'return': int})
        self.assertEqual(fn.__annotations__, {'return': int})
        self.assertIs(inspect.signature(fn).return_annotation, int)

    def test_annotations_str_forward_ref_resolves(self):
        code, base_ns = build_expr(
            [["CONST", 42], ["RETURN", 1]], values={'MyType': int})
        fspec = FormalsSpec(('a',), None, ())
        fn = Function(code, base_ns, fspec, {'a': 'MyType'})
        self.assertIs(fn.__annotations__['a'], int)
        self.assertIs(inspect.signature(fn).parameters['a'].annotation, int)

    def test_annotations_str_forward_ref_dotted(self):
        code, base_ns = build_expr(
            [["CONST", 42], ["RETURN", 1]], values={'MyType': int})
        fspec = FormalsSpec(('a',), None, ())
        fn = Function(code, base_ns, fspec, {'a': 'MyType.__name__'})
        self.assertEqual(fn.__annotations__['a'], 'int')

    def test_annotations_str_unresolved_stays_string(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), None, ())
        fn = Function(code, base_ns, fspec, {'a': 'NoSuchThing'})
        self.assertEqual(fn.__annotations__, {'a': 'NoSuchThing'})

    def test_annotations_str_unresolved_never_retried(self):
        # Late is declared but unbound at first access; the resolver
        # skips the empty slot, the annotation stays a string, and a
        # later binding does not upgrade the cached result
        code, base_ns = build_expr(
            [["CONST", 42], ["RETURN", 1]], local_syms=('Late',))
        fspec = FormalsSpec(('a',), None, ())
        fn = Function(code, base_ns, fspec, {'a': 'Late'})
        self.assertEqual(fn.__annotations__, {'a': 'Late'})
        base_ns.store('Late', str)
        self.assertEqual(fn.__annotations__, {'a': 'Late'})

    def test_annotations_cache_is_stable(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), None, ())
        fn = Function(code, base_ns, fspec, {'a': int})
        self.assertIs(fn.__annotations__, fn.__annotations__)

    def test_annotations_creation_dict_not_mutated(self):
        code, base_ns = build_expr(
            [["CONST", 42], ["RETURN", 1]], values={'MyType': int})
        fspec = FormalsSpec(('a',), None, ())
        creation = {'a': 'MyType'}
        fn = Function(code, base_ns, fspec, creation)
        self.assertIs(fn.__annotations__['a'], int)
        self.assertEqual(creation, {'a': 'MyType'})

    def test_signature_reports_keyword_default(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        fn = code.function(fspec({'y': 42}), base_ns)
        sig = inspect.signature(fn)
        self.assertIs(sig.parameters['x'].default, inspect.Parameter.empty)
        self.assertEqual(sig.parameters['y'].default, 42)

    def test_signature_parameter_kinds(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(
            ('a',), None, ('b',), ('k',), 'args', 'kwargs')
        fn = code.function(fspec(), base_ns)
        sig = inspect.signature(fn)
        self.assertEqual(
            list(sig.parameters.keys()), ['a', 'b', 'args', 'k', 'kwargs'])
        self.assertIs(sig.parameters['a'].kind,
                      inspect.Parameter.POSITIONAL_OR_KEYWORD)
        self.assertIs(sig.parameters['b'].kind,
                      inspect.Parameter.POSITIONAL_OR_KEYWORD)
        self.assertIs(sig.parameters['args'].kind,
                      inspect.Parameter.VAR_POSITIONAL)
        self.assertIs(sig.parameters['k'].kind,
                      inspect.Parameter.KEYWORD_ONLY)
        self.assertIs(sig.parameters['kwargs'].kind,
                      inspect.Parameter.VAR_KEYWORD)

    def test_signature_pos_only_fallback_rebuild(self):
        # pos-only names sort after the POSITIONAL_OR_KEYWORD group in
        # the first build attempt, which inspect.Signature rejects; the
        # builder retries with pos-only emitted as POSITIONAL_OR_KEYWORD
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), ('p',), ())
        fn = code.function(fspec(), base_ns)
        sig = inspect.signature(fn)
        self.assertEqual(list(sig.parameters.keys()), ['a', 'p'])
        self.assertIs(sig.parameters['p'].kind,
                      inspect.Parameter.POSITIONAL_OR_KEYWORD)

    def test_signature_annotated_var_params(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('a',), None, (), (), 'args', 'kwargs')
        fn = Function(code, base_ns, fspec,
                      {'args': int, 'kwargs': str})
        sig = inspect.signature(fn)
        self.assertIs(sig.parameters['args'].annotation, int)
        self.assertIs(sig.parameters['kwargs'].annotation, str)
        self.assertIs(sig.parameters['a'].annotation,
                      inspect.Parameter.empty)

    def test_dunder_name_from_code(self):
        code, base_ns = build_expr(
            [["CONST", 42], ["RETURN", 1]], name="myfn")
        fspec = FormalsSpec(('x',), None, ())
        fn = code.function(fspec(), base_ns)
        self.assertEqual(fn.__name__, 'myfn')

    def test_dunder_module_is_none(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ())
        fn = code.function(fspec(), base_ns)
        self.assertIsNone(fn.__module__)


class TestInspectBoundMethod(unittest.TestCase):
    """
    Bound KosBoundMethod: signature omits receiver (like types.MethodType)
    """

    def test_bound_signature_drops_self(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('self',), None, ())
        fn = code.function(fspec(), base_ns)
        sig_unbound = inspect.signature(fn)
        self.assertIn('self', sig_unbound.parameters)

        bm = fn.__get__(object(), type(None))
        sig_bound = inspect.signature(bm)
        self.assertEqual(list(sig_bound.parameters.keys()), [])

    def test_bound_signature_one_arg_after_self(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('self',), None, ('x',))
        fn = code.function(fspec(), base_ns)
        bm = fn.__get__(object(), type(None))
        sig_bound = inspect.signature(bm)
        self.assertEqual(list(sig_bound.parameters.keys()), ['x'])

    def test_bound_method_annotations_match_function(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('self',), None, ('x',))
        fn = Function(code, base_ns, fspec, {'x': int})
        bm = fn.__get__(object(), type(None))
        self.assertEqual(bm.__annotations__, {'x': int})

    def test_bound_method_signature_keeps_return_annotation(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('self',), None, ('x',))
        fn = Function(code, base_ns, fspec, {'x': int, 'return': str})
        bm = fn.__get__(object(), type(None))
        sig = inspect.signature(bm)
        self.assertEqual(list(sig.parameters.keys()), ['x'])
        self.assertIs(sig.parameters['x'].annotation, int)
        self.assertIs(sig.return_annotation, str)


# The end.
