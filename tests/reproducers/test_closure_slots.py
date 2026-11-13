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
tests.reproducers.test_closure_slots

A NamespaceSpec records, for each of its closure names, the slot the
name has in the scope its own scope is written in. The record is made
when the Code holding the spec is built, for an inline scope and for a
function constant alike; it is not an argument of the spec and is not
stored in a .kio, so a loaded Code records it again.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import io
import unittest

import pytest

from koskari.lib import FunctionSpec, NamespaceSpec
from koskari.module import load_module_source_async, new_module
from koskari.serialize import compile_ki_source_to_kio, load_kio


pytestmark = pytest.mark.phase_reproducers


SRC = """
def outer [a b]:
    var f (lambda [] (+ a b))
    def inner [c]:
        (+ a c)
    let [[d 1]]:
        var g (lambda [] (+ a d))
        #[f inner g]

def with_let [a b]:
    let [[c (+ a 1)]]:
        (+ a b c)

def produce [items]:
    for-each [v items]:
        shift k:
            #(v k)
    None
"""


KIO_SRC = """
def adder [a]:
    (lambda [x] (+ a x))

def with_let [a b]:
    let [[c (+ a 1)]]:
        (+ a b c)

def produce [items]:
    for-each [v items]:
        shift k:
            #(v k)
    None
"""


def specs_in(code, found=None):
    """
    Every NamespaceSpec constant of code and of the functions written
    in it, by name, a function's being its code's own spec
    """

    found = {} if found is None else found

    def visit(arg):
        if isinstance(arg, NamespaceSpec):
            found[arg.name] = arg
        elif isinstance(arg, FunctionSpec):
            found[arg.code.namespace_spec.name] = arg.code.namespace_spec
            specs_in(arg.code, found)
        elif isinstance(arg, tuple):
            for item in arg:
                visit(item)

    for _op, arg in code.soften():
        visit(arg)
    return found


def slot_names(spec, enclosing):
    """
    The name the enclosing spec holds at each slot spec records
    """

    names = (tuple(enclosing.local_syms) + tuple(enclosing.closure_syms)
             + tuple(enclosing.global_syms))
    return tuple(names[slot] for slot in spec.closure_slots)


class TestClosureSlots(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("closure_slots")
        vars(module)["__file__"] = "<closure_slots>"
        await load_module_source_async(module, SRC)
        return vars(module)


    def test_a_new_spec_records_nothing(self):
        spec = NamespaceSpec("s", ("x",), ("a", "b"), ())
        self.assertIsNone(spec.closure_slots)


    def test_slots_are_not_an_argument(self):
        with self.assertRaises(TypeError):
            NamespaceSpec("s", ("x",), ("a", "b"), (), (0, 1))


    async def test_lambda_and_inner_function(self):
        mod = await self.load()
        outer = mod["outer"].code.namespace_spec
        f, inner, g = await mod["outer"](1, 2)

        self.assertEqual(("a", "b"), tuple(f.code.namespace_spec.closure_syms))
        self.assertEqual(
            ("a", "b"), slot_names(f.code.namespace_spec, outer))
        self.assertEqual(
            ("a",), slot_names(inner.code.namespace_spec, outer))
        self.assertEqual(3, await f())
        self.assertEqual(11, await inner(10))


    async def test_function_written_in_a_let(self):
        mod = await self.load()
        scopes = specs_in(mod["outer"].code)
        f, inner, g = await mod["outer"](1, 2)

        self.assertEqual(
            tuple(g.code.namespace_spec.closure_syms),
            slot_names(g.code.namespace_spec, scopes["let"]))
        self.assertEqual(2, await g())


    async def test_let_scope(self):
        mod = await self.load()
        code = mod["with_let"].code
        let = specs_in(code)["let"]

        self.assertEqual(("a", "b"), tuple(let.closure_syms))
        self.assertEqual(("a", "b"), slot_names(let, code.namespace_spec))
        self.assertEqual(5, await mod["with_let"](1, 2))


    async def test_shift_handler(self):
        mod = await self.load()
        code = mod["produce"].code
        handler = specs_in(code)["<handler>"]

        self.assertEqual(("v",), tuple(handler.closure_syms))
        self.assertEqual(("v",), slot_names(handler, code.namespace_spec))


    def test_a_loaded_kio_records_them_again(self):
        buf = io.BytesIO()
        compile_ki_source_to_kio(KIO_SRC, buf)
        buf.seek(0)
        kio = load_kio(buf)

        found = {}
        for code in kio.codes:
            specs_in(code, found)

        self.assertEqual(
            ("a",), slot_names(found["<lambda>"], found["adder"]))
        self.assertEqual(
            ("a", "b"), slot_names(found["let"], found["with_let"]))
        self.assertEqual(
            ("v",), slot_names(found["<handler>"], found["produce"]))


# The end.
