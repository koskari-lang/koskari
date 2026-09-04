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
Tests for the CALL_FAST elector: a call site whose fast slot holds an exact
KosFunction elects the simple-call body into its cache slot and keeps
dispatching to it; a site whose callable changes type re-elects or, after
enough churn, demotes to the generic ladder. Every shape must return the
same results as the ladder alone.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.lib import FormalsSpec, NamespaceSpec
from tests import make_callable
from tests.lib.opcode import CevalTest, build_code

import pytest


pytestmark = pytest.mark.phase_lib


def make_adder():
    """
    A KosFunction of two positionals returning their sum: the simple
    shape the elector serves
    """

    code = build_code([
        ["FAST", 0],
        ["FAST", 1],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ], local_syms=("a", "b"))
    nspec = NamespaceSpec(None, ("a", "b"), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(("a", "b")))


def make_kw_adder():
    """
    A KosFunction whose formals put the elector's simple shape out of
    reach: a keyword-only parameter
    """

    code = build_code([
        ["FAST", 0],
        ["FAST", 1],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ], local_syms=("a", "b"))
    nspec = NamespaceSpec(None, ("a", "b"), ())
    return make_callable(
        code, nspec.get_namespace(None),
        FormalsSpec(("a",), None, None, ("b",)))


def make_yielder():
    """
    A generator KosFunction of two positionals: the generator gate, never
    a frame install
    """

    code = build_code([
        ["FAST", 0],
        ["YIELD", 1],
        ["POP", 1],
        ["FAST", 1],
        ["YIELD", 1],
        ["POP", 1],
        ["RETURN", 0],
    ], local_syms=("a", "b"))
    nspec = NamespaceSpec(None, ("a", "b"), ())
    return make_callable(code, nspec.get_namespace(None), FormalsSpec(("a", "b")))


def make_site(cache=True):
    """
    A function of one positional (f) that calls f twice from its fast slot,
    once with (1, 2) and once with (10, 20), and returns the pair. Each
    CALL_FAST site holds one cache slot when caching is on.

    :return: (function, code)
    """

    nspec = NamespaceSpec(None, ("f",), ())
    instr = (
        ["CONST", 1],
        ["CONST", 2],
        ["CALL_FAST", (0, 2, 0, 0)],
        ["CONST", 10],
        ["CONST", 20],
        ["CALL_FAST", (0, 2, 0, 0)],
        ["RETURN", 2],
    )
    code = build_code(list(instr), local_syms=("f",), name="site", cache=cache)
    fn = make_callable(code, nspec.get_namespace(None), FormalsSpec(("f",)))
    return fn, code


def py_adder(a, b):
    return a + b


class TestCallFastElector(CevalTest):

    def test_call_fast_requests_one_cache_slot(self):
        """
        Each CALL_FAST site reserves one per-site cache slot when caching
        is on, and none when it is gated off
        """

        _, code = make_site(cache=True)
        self.assertTrue(code.caches)
        self.assertEqual(code.cache_slots, 2)

        _, code = make_site(cache=False)
        self.assertFalse(code.caches)
        self.assertEqual(code.cache_slots, 0)


    async def test_monomorphic_kosfunction_site(self):
        """
        Repeated calls through a site whose slot holds one simple
        KosFunction return the sum every time
        """

        site, _ = make_site()
        adder = make_adder()
        for _ in range(20):
            self.assertEqual(await site(adder), (3, 30))


    async def test_ungated_site_matches(self):
        """
        With caching gated off the site returns the same results
        """

        site, _ = make_site(cache=False)
        adder = make_adder()
        for _ in range(20):
            self.assertEqual(await site(adder), (3, 30))


    async def test_different_kosfunctions_share_the_election(self):
        """
        Two simple KosFunctions of the same shape alternate through one
        site: the guard is on the type, and each call binds its own
        function
        """

        site, _ = make_site()
        first = make_adder()
        second = make_adder()
        for _ in range(10):
            self.assertEqual(await site(first), (3, 30))
            self.assertEqual(await site(second), (3, 30))


    async def test_elected_site_takes_a_non_simple_kosfunction(self):
        """
        A KosFunction of the guard type that does not fit the simple shape
        falls to the generic body: a keyword-only formal at full positional
        supply is an arity error, as it is without the election
        """

        site, _ = make_site()
        adder = make_adder()
        self.assertEqual(await site(adder), (3, 30))
        with self.assertRaises(TypeError):
            await site(make_kw_adder())
        self.assertEqual(await site(adder), (3, 30))


    async def test_elected_site_takes_a_generator_function(self):
        """
        A generator function at an elected site completes with the new
        generator, never a frame install
        """

        site, _ = make_site()
        adder = make_adder()
        self.assertEqual(await site(adder), (3, 30))
        gen_a, gen_b = await site(make_yielder())
        self.assertEqual([v async for v in gen_a], [1, 2])
        self.assertEqual([v async for v in gen_b], [10, 20])


    async def test_polymorphic_site_re_elects(self):
        """
        A site alternating between a KosFunction and a Python function
        returns the right sums for both throughout the churn, before and
        after the miss limit demotes it
        """

        site, _ = make_site()
        adder = make_adder()
        for _ in range(30):
            self.assertEqual(await site(adder), (3, 30))
            self.assertEqual(await site(py_adder), (3, 30))


    async def test_demoted_site_still_serves_kosfunctions(self):
        """
        After more Python-callable misses than the limit, a site keeps
        returning the right sums for a KosFunction
        """

        site, _ = make_site()
        adder = make_adder()
        self.assertEqual(await site(adder), (3, 30))
        for _ in range(20):
            self.assertEqual(await site(py_adder), (3, 30))
        for _ in range(20):
            self.assertEqual(await site(adder), (3, 30))


    async def test_non_callable_at_elected_site_raises(self):
        """
        A non-callable in the slot raises TypeError after the election as
        before it
        """

        site, _ = make_site()
        self.assertEqual(await site(make_adder()), (3, 30))
        with self.assertRaises(TypeError):
            await site(42)
        self.assertEqual(await site(make_adder()), (3, 30))


    async def test_callee_error_propagates_through_the_elector(self):
        """
        An exception raised inside the elected callee propagates, and the
        site serves the next call
        """

        adder = make_adder()
        site, _ = make_site()
        self.assertEqual(await site(adder), (3, 30))

        bad_nspec = NamespaceSpec(None, ("f",), ())
        bad = build_code([
            ["CONST", "x"],
            ["CONST", 2],
            ["CALL_FAST", (0, 2, 0, 0)],
            ["RETURN", 1],
        ], local_syms=("f",))
        bad_site = make_callable(
            bad, bad_nspec.get_namespace(None), FormalsSpec(("f",)))
        with self.assertRaises(TypeError):
            await bad_site(adder)
        with self.assertRaises(TypeError):
            await bad_site(adder)


# The end.
