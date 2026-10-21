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
tests.types.test_parameterize_cm

Contract tests for the host-side parameterize context manager

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import Domain, ParamRef, parameter
from koskari.types import parameterize


pytestmark = pytest.mark.phase_lib


class TestParameterizeCM(unittest.TestCase):


    def test_binds_in_thread_default_when_none_active(self):
        default = Domain.current_domain()
        with parameterize(pcm_key="hello"):
            self.assertIs(Domain.current_domain(), default)
            self.assertEqual(parameter("pcm_key", None), "hello")
        self.assertIs(Domain.current_domain(), default)
        self.assertIsNone(parameter("pcm_key", None))


    def test_reuses_active_domain(self):
        with Domain() as dom:
            with parameterize(pcm_key=1):
                self.assertIs(Domain.current_domain(), dom)
                self.assertEqual(parameter("pcm_key", None), 1)
            self.assertIs(Domain.current_domain(), dom)


    def test_restores_prior_value(self):
        with Domain():
            ref = ParamRef("pcm_key")
            ref.store("outer")
            with parameterize(pcm_key="inner"):
                self.assertEqual(ref.deref(), "inner")
            self.assertEqual(ref.deref(), "outer")


    def test_clears_previously_unbound(self):
        with Domain():
            ref = ParamRef("pcm_key")
            self.assertTrue(ref.empty)
            with parameterize(pcm_key="transient"):
                self.assertEqual(ref.deref(), "transient")
            self.assertTrue(ref.empty)


    def test_restores_on_exception(self):
        with Domain():
            ref = ParamRef("pcm_key")
            ref.store("outer")
            with self.assertRaises(RuntimeError):
                with parameterize(pcm_key="inner"):
                    raise RuntimeError("boom")
            self.assertEqual(ref.deref(), "outer")


    def test_multiple_bindings(self):
        with parameterize(pcm_a=1, pcm_b=2):
            self.assertEqual(parameter("pcm_a", None), 1)
            self.assertEqual(parameter("pcm_b", None), 2)


    def test_out_of_order_exit_clears_when_last_leaves(self):
        with Domain():
            ref = ParamRef("pcm_key")
            a = parameterize(pcm_key=1)
            b = parameterize(pcm_key=2)
            a.__enter__()
            b.__enter__()
            a.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 2)
            b.__exit__(None, None, None)
            self.assertTrue(ref.empty)


    def test_out_of_order_exit_restores_prior(self):
        with Domain():
            ref = ParamRef("pcm_key")
            ref.store("outer")
            a = parameterize(pcm_key=1)
            b = parameterize(pcm_key=2)
            a.__enter__()
            b.__enter__()
            a.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 2)
            b.__exit__(None, None, None)
            self.assertEqual(ref.deref(), "outer")


    def test_nested_exit_restores_in_order(self):
        with Domain():
            ref = ParamRef("pcm_key")
            a = parameterize(pcm_key=1)
            b = parameterize(pcm_key=2)
            a.__enter__()
            b.__enter__()
            b.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 1)
            a.__exit__(None, None, None)
            self.assertTrue(ref.empty)


# The end.
