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
Tests for KosProperty: subtype of property that auto-wraps KosCallable
accessors in Demand at registration time.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

from koskari.lib import FormalsSpec, KosProperty, demand
from tests import make_callable
from tests.lib.opcode import build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


def _make_const_fn(value, arity=0):
    """
    Build a KosFunction that ignores all positional args and returns value.

    :param value: Constant to return.
    :param arity: Number of positional parameters (all ignored).
    :return: KosFunction instance.
    """

    code, base_ns = build_expr([
        ["CONST", value],
        ["RETURN", 1],
    ])
    param_names = tuple("_p%d" % i for i in range(arity))
    fspec = FormalsSpec(param_names, None, ())
    return code.function(fspec(), base_ns)


class TestKosPropertyTypeHierarchy(unittest.TestCase):
    """
    KosProperty is a subtype of the built-in property.
    """

    def test_isinstance_property(self):
        """
        isinstance(KosProperty(fn), property) is True
        """

        fn = _make_const_fn(1, arity=1)
        prop = KosProperty(fn)
        self.assertIsInstance(prop, property)


    def test_type_is_kosproperty(self):
        """
        type(KosProperty(fn)) is KosProperty, not property
        """

        fn = _make_const_fn(1, arity=1)
        prop = KosProperty(fn)
        self.assertIs(type(prop), KosProperty)


    def test_kosproperty_is_subclass_of_property(self):
        """
        issubclass(KosProperty, property) is True
        """

        self.assertTrue(issubclass(KosProperty, property))


class TestKosPropertyAccessorWrapping(unittest.TestCase):
    """
    KosCallable accessors are wrapped in Demand; Python callables pass through.
    """

    def test_koscallable_fget_wrapped(self):
        """
        A KosFunction passed as fget is stored as Demand(fn), with __wrapped__ == fn
        """

        fn = _make_const_fn(42, arity=1)
        prop = KosProperty(fn)
        self.assertIsInstance(prop.fget, demand)
        self.assertIs(prop.fget.__wrapped__, fn)


    def test_koscallable_fset_wrapped(self):
        """
        A KosFunction passed as fset is stored as Demand(fn)
        """

        fn = _make_const_fn(None, arity=2)
        prop = KosProperty(None, fn)
        self.assertIsInstance(prop.fset, demand)
        self.assertIs(prop.fset.__wrapped__, fn)


    def test_koscallable_fdel_wrapped(self):
        """
        A KosFunction passed as fdel is stored as Demand(fn)
        """

        fn = _make_const_fn(None, arity=1)
        prop = KosProperty(None, None, fn)
        self.assertIsInstance(prop.fdel, demand)
        self.assertIs(prop.fdel.__wrapped__, fn)


    def test_python_callable_fget_passthrough(self):
        """
        A plain Python callable passed as fget is stored unchanged (no Demand wrap)
        """

        def py_fn(self):
            return 99

        prop = KosProperty(py_fn)
        self.assertIs(prop.fget, py_fn)
        self.assertNotIsInstance(prop.fget, demand)


    def test_none_fget_stored_as_none(self):
        """
        KosProperty() with no fget stores None for fget
        """

        prop = KosProperty()
        self.assertIsNone(prop.fget)


    def test_demand_wrapped_fn_is_idempotent(self):
        """
        Passing demand(fn) as fget results in the same Demand object (no double-wrap)
        """

        fn = _make_const_fn(7, arity=1)
        d = demand(fn)
        prop = KosProperty(d)
        self.assertIs(prop.fget, d)


class TestKosPropertySetterDeleter(unittest.TestCase):
    """
    .setter and .deleter return KosProperty instances with wrapped accessors.
    """

    def test_setter_returns_kosproperty(self):
        """
        prop.setter(fn) returns a KosProperty, not a plain property
        """

        getter = _make_const_fn(1, arity=1)
        setter = _make_const_fn(None, arity=2)

        prop = KosProperty(getter)
        prop2 = prop.setter(setter)

        self.assertIs(type(prop2), KosProperty)


    def test_setter_wraps_koscallable(self):
        """
        prop.setter(kos_fn) stores Demand(kos_fn) as fset
        """

        getter = _make_const_fn(1, arity=1)
        setter = _make_const_fn(None, arity=2)

        prop = KosProperty(getter).setter(setter)

        self.assertIsInstance(prop.fset, demand)
        self.assertIs(prop.fset.__wrapped__, setter)


    def test_setter_preserves_getter(self):
        """
        prop.setter(fn) retains the original fget (still Demand-wrapped)
        """

        getter = _make_const_fn(1, arity=1)
        setter = _make_const_fn(None, arity=2)

        prop = KosProperty(getter)
        original_fget = prop.fget
        prop2 = prop.setter(setter)

        self.assertIsInstance(prop2.fget, demand)
        self.assertIs(prop2.fget.__wrapped__, getter)
        self.assertIs(prop2.fget, original_fget)


    def test_deleter_returns_kosproperty(self):
        """
        prop.deleter(fn) returns a KosProperty
        """

        getter = _make_const_fn(1, arity=1)
        deleter = _make_const_fn(None, arity=1)

        prop = KosProperty(getter).deleter(deleter)

        self.assertIs(type(prop), KosProperty)


    def test_deleter_wraps_koscallable(self):
        """
        prop.deleter(kos_fn) stores Demand(kos_fn) as fdel
        """

        getter = _make_const_fn(1, arity=1)
        deleter = _make_const_fn(None, arity=1)

        prop = KosProperty(getter).deleter(deleter)

        self.assertIsInstance(prop.fdel, demand)
        self.assertIs(prop.fdel.__wrapped__, deleter)


class TestKosPropertyDescriptorEval(unittest.TestCase):
    """
    KosProperty used as a class descriptor invokes KosCallable accessors
    synchronously via the Demand path.
    """

    def test_getter_returns_constant(self):
        """
        Attribute read on a class with a KosFunction getter returns the right value
        """

        getter = _make_const_fn(42, arity=1)

        class Foo:
            x = KosProperty(getter)

        self.assertEqual(Foo().x, 42)


    def test_getter_receives_self(self):
        """
        Getter receives the instance as its first argument
        """

        # Build fn(self) -> self + 1
        code, base_ns = build_expr([
            ["VAR", "self"],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        fspec = FormalsSpec(("self",), None, ())
        fn = code.function(fspec(), base_ns)

        class Counter(int):
            value = KosProperty(fn)

        obj = Counter(99)
        self.assertEqual(obj.value, 100)


    def test_python_getter_unchanged(self):
        """
        Property with a plain Python getter works identically to built-in property
        """

        class Foo:
            def __init__(self):
                self._x = 7

            @KosProperty
            def x(self):
                return self._x

        self.assertEqual(Foo().x, 7)


    def test_setter_via_property_syntax(self):
        """
        @KosProperty / @prop.setter syntax produces a working setter round-trip
        """

        # Build getter: fn(self) -> self._store
        # For simplicity, test through Python callable setter + Koskari getter
        class Bag:
            _v = 0

            @KosProperty
            def v(self):
                return self._v

            @v.setter
            def v(self, val):
                self._v = val

        b = Bag()
        b.v = 99
        self.assertEqual(b.v, 99)


# The end.
