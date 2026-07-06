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
Tests for KosPartial: a standalone native KosCallable with the behavior of
functools.partial (duck-typed, not an isinstance match).

Covers partial behavior (binding, calling, flattening, pickling, __dict__) and
the KosCallable fast path: calling a KosPartial from koskari bytecode dispatches
through the install slot and merges bound + call-site args.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import functools
import gc
import pickle
import unittest

from koskari.lib import FormalsSpec, KosPartial, demand_call
from tests import make_callable
from tests.lib.opcode import build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


def _pyadd(a, b):
    """
    Module-level (picklable) helper used as a plain-Python inner callable.
    """

    return a + b


def _make_concat_fn():
    """
    Build a KosFunction add(a, b) -> a + b. With string arguments it is
    order-sensitive, so the result witnesses bound-before-call ordering.

    :return: KosFunction instance.
    """

    code, base_ns = build_expr([
        ["VAR", "a"],
        ["VAR", "b"],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ])
    fspec = FormalsSpec(("a", "b"), None, ())
    return code.function(fspec(), base_ns)


def _call_through_bytecode(callable_obj, *args):
    """
    Build and run a koskari function that calls callable_obj with args via the
    CALL opcode, driven synchronously through demand_call. This exercises the
    KosPartial install fast path.

    :return: The call result.
    """

    _, outer_ns = build_expr(
        [["CONST", 0], ["RETURN", 1]],
        local_syms=("target",),
        values={"target": callable_obj},
    )

    body = [["FAST", 0]]
    for a in args:
        body.append(["CONST", a])
    body.append(["CALL", len(args)])
    body.append(["RETURN", 1])

    outer_code = build_code(
        body,
        local_syms=(),
        closure_syms=("target",),
        name="call_partial",
    )
    outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
    return demand_call(outer_fn)


class TestKosPartialBehavior(unittest.TestCase):
    """
    KosPartial reproduces functools.partial's construction and attributes.
    """

    def test_func_args_keywords(self):
        """
        func / args / keywords expose the bound callable and arguments
        """

        kp = KosPartial(_pyadd, "A", b="B")
        self.assertIs(kp.func, _pyadd)
        self.assertEqual(kp.args, ("A",))
        self.assertEqual(kp.keywords, {"b": "B"})


    def test_not_isinstance_functools_partial(self):
        """
        KosPartial is intentionally NOT in functools.partial's inheritance tree
        """

        kp = KosPartial(_pyadd, 1)
        self.assertNotIsInstance(kp, functools.partial)


    def test_keywords_defaults_to_dict(self):
        """
        keywords is always a dict, empty when none are bound
        """

        kp = KosPartial(_pyadd)
        self.assertEqual(kp.keywords, {})
        self.assertIsInstance(kp.keywords, dict)


    def test_requires_callable(self):
        """
        A non-callable first argument is rejected
        """

        with self.assertRaises(TypeError):
            KosPartial(5)


    def test_requires_argument(self):
        """
        KosPartial() with no arguments is rejected
        """

        with self.assertRaises(TypeError):
            KosPartial()


    def test_nested_flatten(self):
        """
        KosPartial(KosPartial(fn, x), y) collapses to a single partial
        """

        inner = KosPartial(_pyadd, "A", b="B")
        outer = KosPartial(inner, "C", d="D")
        self.assertIs(outer.func, _pyadd)
        self.assertEqual(outer.args, ("A", "C"))
        self.assertEqual(outer.keywords, {"b": "B", "d": "D"})


    def test_instance_dict(self):
        """
        Arbitrary attributes can be set (partial carries a __dict__)
        """

        kp = KosPartial(_pyadd)
        kp.label = "x"
        self.assertEqual(kp.label, "x")
        self.assertEqual(kp.__dict__, {"label": "x"})


    def test_repr(self):
        """
        repr names the type and renders bound args/keywords
        """

        kp = KosPartial(_pyadd, 1, b=2)
        r = repr(kp)
        self.assertIn("KosPartial", r)
        self.assertIn("b=2", r)


    def test_pickle_roundtrip(self):
        """
        A KosPartial over a picklable callable survives pickling
        """

        kp = KosPartial(_pyadd, 1)
        kp.tag = "t"
        restored = pickle.loads(pickle.dumps(kp))
        self.assertEqual(restored(2), 3)
        self.assertEqual(restored.args, (1,))
        self.assertEqual(restored.tag, "t")


class TestKosPartialPythonCall(unittest.TestCase):
    """
    Python-side calls merge bound and call-site arguments.
    """

    def test_positional(self):
        """
        KosPartial(pyfn, 1)(2) == 3
        """

        self.assertEqual(KosPartial(_pyadd, 1)(2), 3)


    def test_keywords_merge(self):
        """
        Bound keywords reach the call (int base=2)
        """

        self.assertEqual(KosPartial(int, base=2)("10"), 2)


class TestKosPartialDispatch(unittest.TestCase):
    """
    Calling a KosPartial from koskari bytecode routes through the install slot.
    """

    def test_flat_positional_args(self):
        """
        KosPartial(kos_fn, "A")("B") via bytecode merges to kos_fn("A", "B")
        """

        kp = KosPartial(_make_concat_fn(), "A")
        self.assertEqual(_call_through_bytecode(kp, "B"), "AB")


    def test_empty_bound_args(self):
        """
        KosPartial(kos_fn) forwards call-site args unchanged
        """

        kp = KosPartial(_make_concat_fn())
        self.assertEqual(_call_through_bytecode(kp, "A", "B"), "AB")


    def test_python_inner_fallback(self):
        """
        A KosPartial over a plain Python callable falls back to the call path
        """

        kp = KosPartial(_pyadd, "A")
        self.assertEqual(_call_through_bytecode(kp, "B"), "AB")


    def test_gc_sane(self):
        """
        KosPartial instances allocate, are GC-tracked, and collect cleanly
        """

        kps = [KosPartial(_make_concat_fn(), "A") for _ in range(50)]
        self.assertTrue(gc.is_tracked(kps[0]))
        del kps
        gc.collect()


# The end.
