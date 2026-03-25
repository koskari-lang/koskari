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

from koskari.lib import symbol
from koskari.types import FormalsSpec

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

    def test_annotations_from_formals_type_bundles(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        meta = {'a': {'type': symbol('int')}, 'b': {'type': symbol('str')}}
        fspec = FormalsSpec(
            ('a',), None, ('b',), None, None, None, meta)
        fn = code.function(fspec(), base_ns)
        self.assertEqual(
            fn.__annotations__,
            {'a': 'int', 'b': 'str'})

    def test_annotations_merge_return_from_code_extras(self):
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        code.extras['return'] = symbol('int')
        fspec = FormalsSpec(('x',), None, ())
        fn = code.function(fspec(), base_ns)
        self.assertEqual(fn.__annotations__, {'return': 'int'})
        self.assertEqual(fn.__annotations__['return'], 'int')

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
        meta = {'x': {'type': symbol('int')}}
        fspec = FormalsSpec(('self',), None, ('x',), None, None, None, meta)
        fn = code.function(fspec(), base_ns)
        bm = fn.__get__(object(), type(None))
        self.assertEqual(bm.__annotations__, {'x': 'int'})


# The end.
