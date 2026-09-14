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
tests.reproducers.test_formals_keyword_hygiene

Defect reproducer, RED until fixed: a parameter a macro template
declares must not capture an identifier the template splices in from
the use site. That holds for a formal named by a symbol and fails for
one named by a keyword, because hygiene renames symbols only: the
binder keyword keeps its literal name, so the spliced use-site
reference resolves to the macro's parameter instead of the caller's
binding, silently and with no error.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define y 99

define-syntax pos-capture:
  syntax-rules ():
    ((_ body) (lambda [y] (begin body)))

define-syntax kw-capture:
  syntax-rules ():
    ((_ body) (lambda [y: 1] (begin body)))

define-syntax kwonly-capture:
  syntax-rules ():
    ((_ body) (lambda [*: () y: 1] (begin body)))

define-syntax kw-own-body:
  syntax-rules ():
    ((_) (lambda [y: 7] y))

define-syntax kwonly-own-body:
  syntax-rules ():
    ((_) (lambda [*: () y: 8] y))

define check-pos []:
  ((pos-capture y) 3)

define check-kw []:
  ((kw-capture y))

define check-kwonly []:
  ((kwonly-capture y))

define check-kw-own []:
  ((kw-own-body))

define check-kwonly-own []:
  ((kwonly-own-body))
"""


class TestFormalsKeywordHygiene(unittest.IsolatedAsyncioTestCase):
    """
    A template parameter named by a keyword shadows no use-site name
    """

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module

    async def test_positional_formal_does_not_capture(self):
        """
        control: a symbol-named formal already leaves the use-site
        reference alone
        """

        mod = await self.load("fkh_pos")
        self.assertEqual(await vars(mod)["check-pos"](), 99)

    async def test_keyword_default_formal_does_not_capture(self):
        """
        a positional default names its parameter with a keyword and
        must not swallow a spliced use-site y
        """

        mod = await self.load("fkh_kw")
        self.assertEqual(await vars(mod)["check-kw"](), 99)

    async def test_kwonly_default_formal_does_not_capture(self):
        """
        the same for a kw-only default
        """

        mod = await self.load("fkh_kwonly")
        self.assertEqual(await vars(mod)["check-kwonly"](), 99)

    async def test_keyword_formal_still_visible_in_its_own_body(self):
        """
        renaming the binder keeps the template's own body reading it
        """

        mod = await self.load("fkh_own")
        self.assertEqual(await vars(mod)["check-kw-own"](), 7)

    async def test_kwonly_formal_still_visible_in_its_own_body(self):
        """
        the same for a kw-only default
        """

        mod = await self.load("fkh_kwonly_own")
        self.assertEqual(await vars(mod)["check-kwonly-own"](), 8)


# The end.
