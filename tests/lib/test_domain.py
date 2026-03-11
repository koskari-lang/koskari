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
Unit tests for Domain context manager

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import Domain, ParamRef
from tests import make_callable
from tests.lib.opcode import build_expr

import pytest

pytestmark = pytest.mark.phase_lib


class TestDomainContextManager(unittest.IsolatedAsyncioTestCase):
    """
    Domain as context manager (with Domain():)
    """

    def test_domain_context_manager_enter_exit(self):
        """
        with Domain(): pass runs without error
        """

        with Domain():
            pass


    def test_domain_paramref_store_deref_from_python(self):
        """
        ParamRef store and deref work from Python inside with Domain()
        """

        p = ParamRef("x", None)
        with Domain():
            p.store(1)
            self.assertEqual(p.deref(), 1)


    async def test_domain_paramref_after_koskari_return(self):
        """
        ParamRef deref works after koskari returns while inside with Domain()
        """

        p = ParamRef("z", None)
        code, ns = build_expr([
            ["CONST", 42],
            ["RETURN", 1],
        ])
        with Domain():
            p.store(1)
            result = await make_callable(code, ns)()
            self.assertEqual(p.deref(), 1)
        self.assertEqual(result, 42)


    def test_domain_nested(self):
        """
        Nested with Domain(): scopes; inner exits first, outer domain current again
        """

        p = ParamRef("x", None)
        with Domain():
            p.store(1)
            with Domain():
                p.store(2)
                self.assertEqual(p.deref(), 2)
            self.assertEqual(p.deref(), 1)


    def test_domain_no_domain_outside_with(self):
        """
        After with Domain(): pass, ParamRef with no default raises
        """

        p = ParamRef("x")
        with Domain():
            pass
        with self.assertRaises(RuntimeError):
            p.deref()


# The end.
