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
Scope matrix: every nesting of module, function, let and class body
against every way of touching a name from the innermost scope

The name x is bound at one level, or at two (shadowing), and the
innermost scope reads it, writes it with setq, declares its own with
define, declares a global with define-global, or stores through a ref
from refq. Expectations follow the documented resolution model: a read
or write resolves to the nearest enclosing binding; a class body's own
bindings are not visible to the functions defined in it; define makes a
new binding in the current scope; a write from inside a function
reaches the module only through define-global, which conflicts with a
binding declared in the same scope; refq with no enclosing binding
yields the module's cell.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from itertools import combinations

import pytest

from koskari.exceptions import CompilerError

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


CHAINS = (
    ("mod", "fn"),
    ("mod", "let"),
    ("mod", "class", "fn"),
    ("mod", "fn", "fn"),
    ("mod", "fn", "let"),
    ("mod", "let", "fn"),
    ("mod", "let", "let"),
    ("mod", "fn", "let", "fn"),
    ("mod", "fn", "class", "fn"),
    ("mod", "let", "let", "fn"),
    ("mod", "fn", "let", "let", "fn"),
    ("mod", "fn", "fn", "let", "fn"),
    ("mod", "let", "fn", "class", "fn"),
)


ACCESS = {
    "read": "x",
    "setq": '(begin (setq x "new") x)',
    "define": '(begin (var x "inner") x)',
    "define-global": '(begin (global x "global") x)',
    "ref-store": '(begin ((attr (refq x) store) "ref") x)',
}


def wrap(level, bound, inner):
    """
    Wrap inner in one scope level. Every level but the module evaluates
    to a two-element list: the inner result, then x as this level sees
    it afterwards (None when not bound here). The module level returns
    the inner result; the test reads the module binding directly
    """

    after = "x" if bound else "None"
    bind = f'(var x "{level}")' if bound else ""

    if level == "mod":
        return f"(begin {bind} {inner})"
    if level == "fn":
        return f"((lambda [] {bind} (var r {inner}) #[r {after}]))"
    if level == "let":
        binding = '[[x "let"]]' if bound else "[[unused 0]]"
        return f"(let {binding} (var r {inner}) #[r {after}])"
    if level == "class":
        return (
            f"(begin (var K (build-class K () {bind}"
            f" (var m (lambda [self] {inner}))))"
            f" #[((attr (K) m)) None])")
    raise ValueError(level)


def build(chain, bound, access):
    source = ACCESS[access]
    for level, is_bound in reversed(list(zip(chain, bound))):
        source = wrap(level, is_bound, source)
    return source


def flatten(result, depth):
    """
    Unnest the per-level lists into (access value, [x after at each
    non-module level, innermost first])
    """

    afters = []
    value = result
    for _ in range(depth):
        value, after = value
        afters.append(after)
    afters.reverse()
    return value, afters


def expected(chain, bound, access):
    """
    Return ("error", exc_type) or ("value", seen, afters, module_after)
    where afters lists x at each non-module level innermost first
    """

    levels = list(zip(chain, bound))
    inner_bound = bound[-1]

    # the binding each level ends with; None where unbound. The module
    # is index 0.
    after = [level if is_bound else None for level, is_bound in levels]

    # nearest enclosing binding visible from the innermost scope: a
    # class body's bindings are not visible to its methods
    nearest = None
    for index in range(len(levels) - 1, -1, -1):
        level, is_bound = levels[index]
        if is_bound and level != "class":
            nearest = index
            break

    if access == "read":
        if nearest is None:
            return ("error", NameError)
        seen = after[nearest]

    elif access == "setq":
        # a write from inside a function reaches the module only
        # through define-global; a module-level let may write a global
        inside_function = any(level in ("fn", "class") for level in chain)
        if nearest is None or (nearest == 0 and inside_function):
            return ("error", CompilerError)
        after[nearest] = "new"
        seen = "new"

    elif access == "define":
        if inner_bound:
            after[-1] = "inner"
        seen = "inner"

    elif access == "define-global":
        if inner_bound:
            return ("error", CompilerError)
        after[0] = "global"
        seen = "global"

    elif access == "ref-store":
        target = 0 if nearest is None else nearest
        after[target] = "ref"
        seen = "ref"

    else:
        raise ValueError(access)

    module_after = after[0]
    afters = list(reversed(after[1:]))
    # a class level reports None for x
    for position, level in enumerate(reversed(chain[1:])):
        if level == "class":
            afters[position] = None
    return ("value", seen, afters, module_after)


def placements(chain):
    count = len(chain)
    for index in range(count):
        bound = [False] * count
        bound[index] = True
        yield tuple(bound)
    for first, second in combinations(range(count), 2):
        bound = [False] * count
        bound[first] = bound[second] = True
        yield tuple(bound)


def cases(access):
    for chain in CHAINS:
        for bound in placements(chain):
            label = ":".join(
                level.upper() if is_bound else level
                for level, is_bound in zip(chain, bound))
            yield label, chain, bound, access


class TestScopeMatrix(SpecialsTestCase):

    async def check(self, access):
        for label, chain, bound, access in cases(access):
            with self.subTest(case=label, access=access):
                source = build(chain, bound, access)
                want = expected(chain, bound, access)
                if want[0] == "error" and want[1] is CompilerError:
                    with self.assertRaises(CompilerError):
                        self.compile_expr(source)
                    continue
                expr, mod = self.compile_expr(source)
                if want[0] == "error":
                    with self.assertRaises(want[1]):
                        await expr()
                    continue
                result = await expr()
                seen, afters = flatten(result, len(chain) - 1)
                _, want_seen, want_afters, want_module = want
                self.assertEqual(want_seen, seen, "value seen inside")
                self.assertEqual(want_afters, afters, "x after, per level")
                self.assertEqual(
                    want_module, getattr(mod, "x", None), "module x after")


    async def test_read(self):
        await self.check("read")


    async def test_setq(self):
        await self.check("setq")


    async def test_define(self):
        await self.check("define")


    async def test_define_global(self):
        await self.check("define-global")


    async def test_ref_store(self):
        await self.check("ref-store")


# The end.
