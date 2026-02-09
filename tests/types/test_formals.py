# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see
# <http://www.gnu.org/licenses/>.

"""
Unit tests for Formals and FormalsSpec types

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

from koskari.lib import (
    Formals, FormalsSpec, Namespace, NamespaceSpec,
)


class TestFormalsSpec(unittest.TestCase):
    """
    Tests for the FormalsSpec type
    """

    def test_create_empty(self):
        """FormalsSpec can be created with no arguments"""

        fspec = FormalsSpec()
        self.assertIsNone(fspec.pos)
        self.assertIsNone(fspec.pos_only)
        self.assertIsNone(fspec.kwd)
        self.assertIsNone(fspec.kwd_only)
        self.assertIsNone(fspec.star)
        self.assertIsNone(fspec.starstar)

    def test_create_with_pos(self):
        """FormalsSpec accepts positional symbols"""

        fspec = FormalsSpec(("a", "b"))
        self.assertEqual(fspec.pos, ("a", "b"))
        self.assertIsNone(fspec.pos_only)
        self.assertIsNone(fspec.kwd)
        self.assertIsNone(fspec.kwd_only)
        self.assertIsNone(fspec.star)
        self.assertIsNone(fspec.starstar)

    def test_create_with_kwd(self):
        """FormalsSpec accepts keyword symbols"""

        fspec = FormalsSpec(("a",), None, ("b",))
        self.assertEqual(fspec.pos, ("a",))
        self.assertIsNone(fspec.pos_only)
        self.assertEqual(fspec.kwd, ("b",))
        self.assertIsNone(fspec.kwd_only)
        self.assertIsNone(fspec.star)
        self.assertIsNone(fspec.starstar)

    def test_create_full(self):
        """FormalsSpec accepts all parameter types"""

        fspec = FormalsSpec(
            ("a",), ("b",), ("c",), ("d",), "args", "kwargs"
        )
        self.assertEqual(fspec.pos, ("a",))
        self.assertEqual(fspec.pos_only, ("b",))
        self.assertEqual(fspec.kwd, ("c",))
        self.assertEqual(fspec.kwd_only, ("d",))
        self.assertEqual(fspec.star, "args")
        self.assertEqual(fspec.starstar, "kwargs")

    def test_call_creates_formals(self):
        """Calling FormalsSpec creates a Formals instance"""

        fspec = FormalsSpec(("a", "b"))
        formals = fspec()
        self.assertIsInstance(formals, Formals)
        self.assertEqual(formals.pos, ("a", "b"))
        self.assertIsNone(formals.defaults)

    def test_call_with_defaults(self):
        """Calling FormalsSpec with defaults creates Formals with defaults"""

        fspec = FormalsSpec(("a",), None, ("b",))
        defaults = {'b': 42}
        formals = fspec(defaults)
        self.assertIsInstance(formals, Formals)
        self.assertEqual(formals.defaults, {'b': 42})

    def test_namespace_spec_empty(self):
        """FormalsSpec.namespace_spec() with empty spec"""

        fspec = FormalsSpec()
        nspec = fspec.namespace_spec(None)
        self.assertIsInstance(nspec, NamespaceSpec)
        self.assertIsNone(nspec.name)
        self.assertIsNone(nspec.local_syms)
        self.assertIsNone(nspec.closure_syms)

    def test_namespace_spec_with_formals(self):
        """FormalsSpec.namespace_spec() includes formals as locals"""

        fspec = FormalsSpec(("a",), None, ("b",))
        nspec = fspec.namespace_spec(None)
        self.assertIsInstance(nspec, NamespaceSpec)
        self.assertEqual(nspec.local_syms, ("a", "b"))

    def test_namespace_spec_with_base(self):
        """FormalsSpec.namespace_spec() merges with base spec"""

        fspec = FormalsSpec(("a",), None, ("b",))
        base = NamespaceSpec("<ns>", ("foo", "bar"), ())
        nspec = fspec.namespace_spec(base)
        self.assertIsNot(nspec, base)
        self.assertEqual(nspec.name, "<ns>")
        self.assertEqual(nspec.local_syms, ("a", "b", "foo", "bar"))
        self.assertEqual(nspec.closure_syms, ())

    def test_namespace_spec_with_star(self):
        """FormalsSpec.namespace_spec() includes star args"""

        fspec = FormalsSpec(("a",), None, None, None, "args", "kwargs")
        nspec = fspec.namespace_spec(None)
        self.assertEqual(nspec.local_syms, ("a", "args", "kwargs"))


class TestFormals(unittest.TestCase):
    """
    Tests for the Formals type
    """

    def test_create_from_spec(self):
        """Formals can be created from FormalsSpec"""

        fspec = FormalsSpec(("a", "b"))
        formals = fspec()
        self.assertIsInstance(formals, Formals)
        self.assertEqual(formals.pos, ("a", "b"))

    def test_create_direct(self):
        """Formals can be created directly with all arguments"""

        formals = Formals(("a",), (), ("b",), (), None, None, None)
        self.assertEqual(formals.pos, ("a",))
        self.assertIsNone(formals.pos_only)
        self.assertEqual(formals.kwd, ("b",))
        self.assertIsNone(formals.kwd_only)
        self.assertIsNone(formals.star)
        self.assertIsNone(formals.starstar)
        self.assertIsNone(formals.defaults)

    def test_fields(self):
        """Formals stores all field types correctly"""

        formals = Formals(
            ("a",), ("b",), ("c",), ("d",), "args", "kwargs", {'c': 99}
        )
        self.assertEqual(formals.pos, ("a",))
        self.assertEqual(formals.pos_only, ("b",))
        self.assertEqual(formals.kwd, ("c",))
        self.assertEqual(formals.kwd_only, ("d",))
        self.assertEqual(formals.star, "args")
        self.assertEqual(formals.starstar, "kwargs")
        self.assertEqual(formals.defaults, {'c': 99})


class TestFormalsApply(unittest.TestCase):
    """
    Tests for Formals.apply() method
    """

    def test_apply_simple_positional(self):
        """apply() binds positional arguments"""

        formals = Formals(("foo", "bar"), (), (), (), None, None, None)
        ns = Namespace("ns", None, ("foo", "bar"))

        formals.apply(ns, (1, 2))
        self.assertEqual(ns["foo"], 1)
        self.assertEqual(ns["bar"], 2)

    def test_apply_with_kwargs(self):
        """apply() binds keyword arguments"""

        formals = Formals(("foo",), (), ("bar",), (), None, None, None)
        ns = Namespace("ns", None, ("foo", "bar"))

        formals.apply(ns, (100,), {"bar": 200})
        self.assertEqual(ns["foo"], 100)
        self.assertEqual(ns["bar"], 200)

    def test_apply_with_star(self):
        """apply() collects extra positional args in star"""

        formals = Formals(("foo",), (), ("bar",), (), "args", None, None)
        ns = Namespace("ns", None, ("foo", "bar", "args"))

        formals.apply(ns, (1, 2, 3, 4))
        self.assertEqual(ns["foo"], 1)
        self.assertEqual(ns["bar"], 2)
        self.assertEqual(ns["args"], (3, 4))

    def test_apply_with_starstar(self):
        """apply() collects extra keyword args in starstar"""

        formals = Formals(("foo",), (), (), (), None, "kwargs", None)
        ns = Namespace("ns", None, ("foo", "kwargs"))

        formals.apply(ns, (1,), {"extra": 42, "more": 99})
        self.assertEqual(ns["foo"], 1)
        self.assertEqual(ns["kwargs"], {"extra": 42, "more": 99})

    def test_apply_with_defaults(self):
        """apply() uses defaults for missing keyword arguments"""

        formals = Formals(
            ("foo",), (), ("bar",), (), None, None, {"bar": 999}
        )
        ns = Namespace("ns", None, ("foo", "bar"))

        formals.apply(ns, (42,))
        self.assertEqual(ns["foo"], 42)
        self.assertEqual(ns["bar"], 999)

    def test_apply_override_default(self):
        """apply() allows overriding defaults"""

        formals = Formals(
            ("foo",), (), ("bar",), (), None, None, {"bar": 999}
        )
        ns = Namespace("ns", None, ("foo", "bar"))

        formals.apply(ns, (42,), {"bar": 123})
        self.assertEqual(ns["foo"], 42)
        self.assertEqual(ns["bar"], 123)

    def test_apply_missing_positional_raises(self):
        """apply() raises TypeError for missing positional argument"""

        formals = Formals(("foo", "bar"), (), (), (), None, None, None)
        ns = Namespace("ns", None, ("foo", "bar"))

        with self.assertRaises(TypeError):
            formals.apply(ns, (1,))

    def test_apply_too_many_positional_raises(self):
        """apply() raises TypeError for too many positional arguments"""

        formals = Formals(("foo",), (), (), (), None, None, None)
        ns = Namespace("ns", None, ("foo",))

        with self.assertRaises(TypeError):
            formals.apply(ns, (1, 2, 3))

    def test_apply_missing_keyword_raises(self):
        """apply() raises TypeError for missing keyword argument"""

        formals = Formals(("foo",), (), ("bar",), (), None, None, None)
        ns = Namespace("ns", None, ("foo", "bar"))

        with self.assertRaises(TypeError):
            formals.apply(ns, (1,))

    def test_apply_unexpected_keyword_raises(self):
        """apply() raises TypeError for unexpected keyword arguments"""

        formals = Formals(("foo",), (), (), (), None, None, None)
        ns = Namespace("ns", None, ("foo",))

        with self.assertRaises(TypeError):
            formals.apply(ns, (1,), {"unexpected": 42})

    def test_apply_empty_star(self):
        """apply() handles empty star args"""

        formals = Formals(("foo",), (), (), (), "args", None, None)
        ns = Namespace("ns", None, ("foo", "args"))

        formals.apply(ns, (1,))
        self.assertEqual(ns["foo"], 1)
        self.assertEqual(ns["args"], ())

    def test_apply_empty_starstar(self):
        """apply() handles empty starstar kwargs"""

        formals = Formals(("foo",), (), (), (), None, "kwargs", None)
        ns = Namespace("ns", None, ("foo", "kwargs"))

        formals.apply(ns, (1,))
        self.assertEqual(ns["foo"], 1)
        self.assertEqual(ns["kwargs"], {})

    def test_callable_interface(self):
        """Formals can be called directly with (ns, args) or (ns, args, kwargs)"""

        formals = Formals(("foo", "bar"), (), (), (), None, None, None)
        ns = Namespace("ns", None, ("foo", "bar"))

        result = formals(ns, (10, 20))
        self.assertIsNone(result)
        self.assertEqual(ns["foo"], 10)
        self.assertEqual(ns["bar"], 20)


class TestFormalsKwdOnly(unittest.TestCase):
    """
    Tests for keyword-only arguments
    """

    def test_kwd_only_with_default(self):
        """Keyword-only arguments work with defaults"""

        formals = Formals(
            ("a",), (), (), ("b",), None, None, {"b": 100}
        )
        ns = Namespace("ns", None, ("a", "b"))

        formals.apply(ns, (1,))
        self.assertEqual(ns["a"], 1)
        self.assertEqual(ns["b"], 100)

    def test_kwd_only_provided(self):
        """Keyword-only arguments can be provided"""

        formals = Formals(
            ("a",), (), (), ("b",), None, None, {"b": 100}
        )
        ns = Namespace("ns", None, ("a", "b"))

        formals.apply(ns, (1,), {"b": 200})
        self.assertEqual(ns["a"], 1)
        self.assertEqual(ns["b"], 200)

    def test_kwd_only_missing_raises(self):
        """Missing keyword-only argument without default raises"""

        formals = Formals(
            ("a",), (), (), ("b",), None, None, None
        )
        ns = Namespace("ns", None, ("a", "b"))

        with self.assertRaises(TypeError):
            formals.apply(ns, (1,))


class TestFormals_PosOnly(unittest.TestCase):
    """
    Tests for positional-only arguments
    """

    def test_pos_only_applied(self):
        """Positional-only arguments are applied correctly"""

        formals = Formals(
            ("a",), ("b",), (), (), None, None, None
        )
        ns = Namespace("ns", None, ("a", "b"))

        formals.apply(ns, (1, 2))
        self.assertEqual(ns["a"], 1)
        self.assertEqual(ns["b"], 2)

    def test_pos_only_missing_raises(self):
        """Missing positional-only argument raises"""

        formals = Formals(
            ("a",), ("b",), (), (), None, None, None
        )
        ns = Namespace("ns", None, ("a", "b"))

        with self.assertRaises(TypeError):
            formals.apply(ns, (1,))


# The end.
