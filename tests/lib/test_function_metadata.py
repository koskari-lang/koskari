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
Writable identity metadata on KosFunction, with read delegation through
Demand and BoundMethod.

__name__, __qualname__, __doc__, __module__, and __annotations__ accept
writes on Function, enabling functools.update_wrapper. Writes land in
metadata override storage; the semantic name that self-binding keys off
stays fixed. Demand delegates reads to the wrapped function; BoundMethod
delegates reads to __func__ and refuses writes, matching CPython bound
methods.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import functools
import unittest

import pytest

from koskari.lib import FormalsSpec
from tests.lib.test_expression_function import build_expr


pytestmark = pytest.mark.phase_lib


def make_fn(name="expr"):
    code, base_ns = build_expr(
        [["CONST", 42], ["RETURN", 1]], name=name)
    fspec = FormalsSpec(('x',), None, ())
    return code.function(fspec(), base_ns)


class TestFunctionMetadataWrites(unittest.TestCase):
    """
    The five wrapper-assignment attrs accept writes on Function
    """

    def test_name_write_and_read(self):
        fn = make_fn()
        fn.__name__ = "renamed"
        self.assertEqual(fn.__name__, "renamed")

    def test_name_write_requires_str(self):
        fn = make_fn()
        with self.assertRaises(TypeError):
            fn.__name__ = 42

    def test_name_write_leaves_semantic_name(self):
        fn = make_fn()
        before = fn.name
        fn.__name__ = "renamed"
        self.assertEqual(fn.name, before)

    def test_name_write_shows_in_repr(self):
        fn = make_fn()
        fn.__name__ = "renamed"
        self.assertIn("renamed", repr(fn))

    def test_qualname_write_and_read(self):
        fn = make_fn()
        fn.__qualname__ = "Outer.renamed"
        self.assertEqual(fn.__qualname__, "Outer.renamed")

    def test_qualname_write_requires_str(self):
        fn = make_fn()
        with self.assertRaises(TypeError):
            fn.__qualname__ = 42

    def test_qualname_follows_name_when_unset(self):
        fn = make_fn()
        fn.__name__ = "renamed"
        self.assertEqual(fn.__qualname__, "renamed")

    def test_doc_write_and_read(self):
        fn = make_fn()
        fn.__doc__ = "written doc"
        self.assertEqual(fn.__doc__, "written doc")

    def test_module_write_and_read(self):
        fn = make_fn()
        self.assertIsNone(fn.__module__)
        fn.__module__ = "some.module"
        self.assertEqual(fn.__module__, "some.module")

    def test_annotations_write_and_read(self):
        fn = make_fn()
        fn.__annotations__ = {'x': int}
        self.assertEqual(fn.__annotations__, {'x': int})

    def test_annotations_write_requires_dict(self):
        fn = make_fn()
        with self.assertRaises(TypeError):
            fn.__annotations__ = "nope"

    def test_annotations_write_refreshes_signature(self):
        fn = make_fn()
        before = fn.__signature__
        self.assertIs(
            before.parameters['x'].annotation,
            before.empty)
        fn.__annotations__ = {'x': int}
        self.assertIs(fn.__signature__.parameters['x'].annotation, int)


class TestUpdateWrapper(unittest.TestCase):
    """
    functools.update_wrapper works between Functions
    """

    def test_update_wrapper_copies_identity(self):
        inner = make_fn(name="inner_fn")
        wrapper = make_fn(name="wrapper_fn")
        inner.__doc__ = "inner doc"
        inner.marker = "m1"

        result = functools.update_wrapper(wrapper, inner)

        self.assertIs(result, wrapper)
        self.assertEqual(wrapper.__name__, "inner_fn")
        self.assertEqual(wrapper.__qualname__, "inner_fn")
        self.assertEqual(wrapper.__doc__, "inner doc")
        self.assertEqual(wrapper.marker, "m1")
        self.assertIs(wrapper.__wrapped__, inner)


class TestBoundMethodDelegation(unittest.TestCase):
    """
    BoundMethod identity metadata reads through __func__ and refuses
    writes
    """

    def make_bound(self):
        fn = make_fn(name="method_fn")
        return fn.__get__(object(), object)

    def test_reads_delegate(self):
        fn = make_fn(name="method_fn")
        fn.__doc__ = "method doc"
        bm = fn.__get__(object(), object)
        self.assertEqual(bm.__name__, fn.__name__)
        self.assertEqual(bm.__qualname__, fn.__qualname__)
        self.assertEqual(bm.__doc__, "method doc")
        self.assertEqual(bm.__module__, fn.__module__)

    def test_writes_refused(self):
        bm = self.make_bound()
        with self.assertRaises(AttributeError):
            bm.__name__ = "nope"
        with self.assertRaises(AttributeError):
            bm.__doc__ = "nope"


# The end.
