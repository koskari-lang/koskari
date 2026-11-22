# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.reproducers.test_generator_effects

What a suspended generator holds outside its own chain. A parameterize
swaps cells into the consumer's table and a try that is handling an
exception puts it on the thread; both go out at every yield and come
back at every re-entry, so the consumer never sees them and the
generator always does. A with runs nothing at a yield.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


STEPS = 50


# Two parameterize extents and two handling trys are open around the
# inner yields: four effects, more than a generator's state holds
# inline. Each yield reports the generator's view; the consumer adds
# its own view while the generator is suspended.
NESTED_SRC = """
(import-from sys exc_info)

(def handled []
  (define e (item (exc_info) 1))
  (if (is e None) "none" (str e)))

(def view [tag]
  #(tag (parameterq a "unset") (parameterq b "unset") (handled)))

(def nested []
  (parameterize [[a "a1"]]
    (try
      (raise (ValueError "outer"))
      ([e ValueError]
        (parameterize [[b "b1"]]
          (try
            (raise (ValueError "inner"))
            ([e2 ValueError]
              (for-each [i (range 0 50)]
                (yield (view i)))))
          (yield (view "mid"))))))
  (yield (view "end")))

(def looped []
  (var out #[])
  (for-each [x (nested)]
    (out.append #(x (view "consumer"))))
  out)

(def stepped []
  (var out #[])
  (var it (nested))
  (try
    (raise (KeyError "own"))
    ([e KeyError]
      (parameterize [[a "c1"]]
        (for-each [i (range 0 52)]
          (out.append #((next it) (view "consumer")))))))
  out)

(var looped-results (looped))
(var stepped-results (stepped))
"""


# The manager's __exit__ runs once, when the block is left, and never
# at a yield.
WITH_SRC = """
(def managed [log]
  (with [c (Ctx log)]
    (for-each [i (range 0 20)]
      (yield (len log))))
  (yield (len log)))

(def looped [log]
  (var out #[])
  (for-each [x (managed log)]
    (out.append x))
  out)
"""


class Ctx:
    """
    A context manager that logs its enter and its exit
    """

    def __init__(self, log):
        self.log = log


    def __enter__(self):
        self.log.append("enter")
        return self


    def __exit__(self, exc_type, exc, tb):
        self.log.append("exit")
        return False


def generator_views():
    """
    The generator's own view at each of its yields
    """

    views = [(i, "a1", "b1", "inner") for i in range(STEPS)]
    views.append(("mid", "a1", "b1", "outer"))
    views.append(("end", "unset", "unset", "none"))
    return views


class TestGeneratorEffects(unittest.IsolatedAsyncioTestCase):
    """
    A generator's parameter cells and handled exceptions leave the
    consumer at every yield and return at every re-entry
    """

    async def load(self, name, source, **injected):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        vars(module).update(injected)
        await load_module_source_async(module, source)
        return module


    async def test_for_each_consumer_never_sees_the_effects(self):
        mod = await self.load("gen_effects_looped", NESTED_SRC)
        consumer = ("consumer", "unset", "unset", "none")
        self.assertEqual(
            vars(mod)["looped-results"],
            [(view, consumer) for view in generator_views()])


    async def test_stepping_consumer_keeps_its_own_effects(self):
        """
        A consumer stepping with next inside its own parameterize and
        its own handler reads its own cell and its own handled
        exception between steps; the generator reads the consumer's
        cell only once its own extent is left
        """

        mod = await self.load("gen_effects_stepped", NESTED_SRC)
        consumer = ("consumer", "c1", "unset", "'own'")
        views = generator_views()
        views[-1] = ("end", "c1", "unset", "'own'")
        self.assertEqual(
            vars(mod)["stepped-results"],
            [(view, consumer) for view in views])


    async def test_python_stepping_sees_no_handled_exception(self):
        mod = await self.load("gen_effects_python", NESTED_SRC)
        gen = mod.nested()
        seen = []
        async for view in gen:
            seen.append(view)
            self.assertIsNone(sys.exc_info()[1])
        self.assertEqual(seen, generator_views())


    async def test_with_runs_nothing_at_a_yield(self):
        mod = await self.load("gen_effects_with", WITH_SRC, Ctx=Ctx)

        log = []
        self.assertEqual(await mod.looped(log), [1] * 20 + [2])
        self.assertEqual(log, ["enter", "exit"])

        log = []
        seen = []
        async for count in mod.managed(log):
            seen.append(count)
        self.assertEqual(seen, [1] * 20 + [2])
        self.assertEqual(log, ["enter", "exit"])


# The end.
