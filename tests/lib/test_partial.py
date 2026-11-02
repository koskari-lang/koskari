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
import sys
import unittest

import pytest

from koskari.lib import FormalsSpec, KosPartial, demand_call
from tests import make_callable
from tests.lib.opcode import build_code, build_expr


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


def _call_names_through_bytecode(callable_obj, pos_args, kwd_names, kwd_vals):
    """
    Build and run a koskari function that calls callable_obj through the
    CALL_NAMES opcode, whose stack is callable, positionals, keyword
    values, then the names tuple, driven synchronously through
    demand_call

    :return: The call result
    """

    _, outer_ns = build_expr(
        [["CONST", 0], ["RETURN", 1]],
        local_syms=("target",),
        values={"target": callable_obj},
    )

    body = [["FAST", 0]]
    for a in pos_args:
        body.append(["CONST", a])
    for v in kwd_vals:
        body.append(["CONST", v])
    body.append(["CONST", tuple(kwd_names)])
    body.append(["CALL_NAMES", len(pos_args)])
    body.append(["RETURN", 1])

    outer_code = build_code(
        body,
        local_syms=(),
        closure_syms=("target",),
        name="call_partial_names",
    )
    outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
    return demand_call(outer_fn)


def _pykw(*args, **kwargs):
    """
    Plain Python inner that reports what it was called with
    """

    return (args, kwargs)


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


    def test_non_str_bound_keyword_releases_the_values_it_held(self):
        """
        A bound keyword dict given a non-str key after construction
        makes the call raise TypeError, and the values the call had
        already taken hold of are released again
        """

        val = object()
        kp = KosPartial(_pyadd, b=val)
        kp.keywords[0] = "x"

        before = sys.getrefcount(val)
        with self.assertRaises(TypeError):
            kp(1)
        self.assertEqual(before, sys.getrefcount(val))


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
        self.assertIn("partial(", r)
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


    def test_bound_keyword_outlives_mutation_during_call(self):
        """
        A bound keyword value reaches the callee even when code the
        callee runs first drops it from the live keywords dict: sorted
        iterates its argument before it hands key on, and the iteration
        rebinds the partial's only reference to the key function
        """

        kp = KosPartial(sorted, key=lambda v: -v)

        class Source:
            def __iter__(self):
                kp.keywords['key'] = None
                return iter([1, 3, 2])

        self.assertEqual(kp(Source()), [3, 2, 1])


    def test_bound_args_outlive_setstate_during_call(self):
        """
        Bound positionals reach the callee after code the callee runs
        replaces the partial's state, which frees the old tuple
        """

        kp = KosPartial(max, [1, 5, 3], key=lambda v: v)

        class Replace:
            def __call__(self, v):
                kp.__setstate__((max, (), {}, None))
                return v

        kp.keywords['key'] = Replace()
        self.assertEqual(kp(), 5)


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


    def test_bound_keywords(self):
        """
        KosPartial(kos_fn, b="B")("A") via bytecode binds b by name
        """

        kp = KosPartial(_make_concat_fn(), b="B")
        self.assertEqual(_call_through_bytecode(kp, "A"), "AB")


    def test_call_keywords(self):
        """
        KosPartial(kos_fn, "A")(b="B") via bytecode binds b by name
        """

        kp = KosPartial(_make_concat_fn(), "A")
        self.assertEqual(
            _call_names_through_bytecode(kp, (), ("b",), ("B",)), "AB")


    def test_call_keyword_overrides_bound(self):
        """
        A call keyword replaces the bound keyword of the same name
        """

        kp = KosPartial(_make_concat_fn(), "A", b="X")
        self.assertEqual(
            _call_names_through_bytecode(kp, (), ("b",), ("B",)), "AB")


    def test_both_sides_override_and_new(self):
        """
        Bound and call keywords merge: the call side replaces a shared
        name and adds a new one, bound names keep their order
        """

        kp = KosPartial(_pykw, 1, x=1, y=2)
        args, kwargs = _call_names_through_bytecode(
            kp, (3,), ("y", "z"), (20, 30))
        self.assertEqual(args, (1, 3))
        self.assertEqual(kwargs, {"x": 1, "y": 20, "z": 30})
        self.assertEqual(list(kwargs), ["x", "y", "z"])


    def test_subclass_key_stored_as_plain_str(self):
        """
        A str subclass key given at creation is stored as a plain str,
        so the merge never compares through its Python methods
        """

        class Loud(str):
            def __hash__(self):
                return str.__hash__(self)

            def __eq__(self, other):
                raise AssertionError("compared through Python")

        kp = KosPartial(_pykw, **{Loud("b"): 1, "c": 2})
        self.assertEqual([type(k) for k in kp.keywords], [str, str])
        args, kwargs = _call_names_through_bytecode(
            kp, (), ("b", "d"), ("B", 4))
        self.assertEqual(args, ())
        self.assertEqual(kwargs, {"b": "B", "c": 2, "d": 4})


    def test_subclass_call_name_matches_by_text(self):
        """
        A str subclass call name binds a bound keyword by its text,
        without its Python methods running
        """

        class Loud(str):
            def __hash__(self):
                raise AssertionError("hashed through Python")

            def __eq__(self, other):
                raise AssertionError("compared through Python")

        kp = KosPartial(_pykw, b=1, c=2)
        args, kwargs = _call_names_through_bytecode(
            kp, (), (Loud("b"),), ("B",))
        self.assertEqual(args, ())
        self.assertEqual(kwargs, {"b": "B", "c": 2})


    def test_non_dict_state_dict_refused(self):
        """
        The fourth state element becomes the instance __dict__, so
        __setstate__ refuses one that is neither None nor a dict and
        leaves the partial as it was
        """

        kp = KosPartial(max, 1)
        kp.tag = "kept"
        with self.assertRaises(TypeError):
            kp.__setstate__((min, (2,), {}, 99999))
        self.assertIs(kp.func, max)
        self.assertEqual((1,), kp.args)
        self.assertEqual("kept", kp.tag)
        kp.__setstate__((min, (2,), {}, {"tag": "set"}))
        self.assertIs(kp.func, min)
        self.assertEqual("set", kp.tag)


    def test_non_str_bound_key_refused(self):
        """
        A non-str key reaching the bound keywords is refused: at
        __setstate__ on the way in, and at the call when inserted
        through the live dict
        """

        kp = KosPartial(_pykw, b=1)
        with self.assertRaises(TypeError):
            kp.__setstate__((_pykw, (), {1: 2}, None))
        kp.keywords[1] = 2
        with self.assertRaises(TypeError):
            _call_names_through_bytecode(kp, (), ("b",), ("B",))
        with self.assertRaises(TypeError):
            kp(b="B")


    def test_gc_sane(self):
        """
        KosPartial instances allocate, are GC-tracked, and collect cleanly
        """

        kps = [KosPartial(_make_concat_fn(), "A") for _ in range(50)]
        self.assertTrue(gc.is_tracked(kps[0]))
        del kps
        gc.collect()


# An element's __repr__ replaces the partial's args and keywords through
# __setstate__ while the repr of the partial is still rendering them.
# Each run is a subprocess under the debug allocator, so a read of the
# released tuple or dict ends the run instead of passing by luck.
REPR_SETSTATE_SCRIPT = """
import gc
import sys

from koskari.lib import KosPartial

MODE, WHICH = sys.argv[1], sys.argv[2]


def f(*a, **k):
    return a, k


class Swapper:
    def __repr__(self):
        p = holder[0]
        if MODE == "args":
            p.__setstate__((f, (), None, None))
        else:
            p.__setstate__((f, (), {}, None))
        gc.collect()
        junk = [object() for _ in range(10000)]
        return "swapped"


holder = [None]
if MODE == "args":
    p = KosPartial(f, Swapper(), *[object() for _ in range(2000)])
else:
    p = KosPartial(f, **{f"k{i}": Swapper() for i in range(2000)})
holder[0] = p
r = repr(p) if WHICH == "repr" else p.__kos_repr__()
print("survived", len(r))
"""


class TestReprAcrossSetstate(unittest.TestCase):
    """
    repr and __kos_repr__ render the func, args and keywords the partial
    held when they began, so a __setstate__ from an element's repr
    cannot release what they are still reading
    """

    def test_repr_survives_setstate_from_element(self):
        import os
        import subprocess

        env = dict(os.environ, PYTHONMALLOC="debug")
        for mode in ("args", "keywords"):
            for which in ("repr", "kos_repr"):
                with self.subTest(mode=mode, which=which):
                    proc = subprocess.run(
                        [sys.executable, "-c", REPR_SETSTATE_SCRIPT,
                         mode, which],
                        capture_output=True, text=True, env=env)
                    self.assertEqual(proc.returncode, 0, proc.stderr)
                    self.assertTrue(
                        proc.stdout.startswith("survived "), proc.stdout)


# The end.
