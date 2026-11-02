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
Tests for the native types after the koskari.lib module object has died

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import subprocess
import sys
import unittest

import pytest


pytestmark = pytest.mark.phase_lib


# A finalizer that runs in the interpreter's last collection, after
# the module object of koskari.lib has been collected with the other
# modules, and uses a static type through a reference of its own. Only
# type objects are bound, never a module-level builtin function, since
# a builtin function keeps its module alive.
SCRIPT = """
import contextvars
import os
import sys

import koskari.lib as kl

OP = sys.argv[1]
K = dict(symbol=kl.symbol, gensym=kl.gensym, pair=kl.pair, nil=kl.nil,
         values=kl.values, Ref=kl.Ref, repr=repr, str=str,
         BaseException=BaseException)
K['pre'] = kl.symbol("pre_existing")


def op_symbol(K=K):
    return K['symbol']("pre_existing") is K['pre']


def op_fresh_symbol(K=K):
    return K['symbol']("fresh_symbol")


def op_gensym(K=K):
    return K['gensym']("g")


def op_pair_repr(K=K):
    return K['repr'](K['pair'](1, K['pair'](2, K['nil'])))


def op_pair_str(K=K):
    return K['str'](K['pair'](1, K['pair'](2, K['nil'])))


def op_values_repr(K=K):
    return K['repr'](K['values'](1, 2, a=3))


def op_ref(K=K):
    return K['repr'](K['Ref']("x", 1))


FN = globals()["op_" + OP]


class Holder:
    def __init__(self):
        self.me = self

    def __del__(self, write=os.write, fn=FN, K=K):
        try:
            result = fn()
        except K['BaseException'] as exc:
            write(2, ("raised " + K['repr'](exc)).encode())
        else:
            write(2, ("ok " + K['repr'](result)).encode())


var = contextvars.ContextVar("holder")
var.set(Holder())
del kl
"""


OPS = (
    "symbol", "fresh_symbol", "gensym", "pair_repr", "pair_str",
    "values_repr", "ref")


class TestTypesOutliveModule(unittest.TestCase):
    """
    The static types and their instances hold no reference to the
    module object, so they stay usable after it is collected
    """

    def run_op(self, op):
        return subprocess.run(
            [sys.executable, "-c", SCRIPT, op],
            capture_output=True, text=True)

    def test_finalizer_after_module_collection(self):
        for op in OPS:
            with self.subTest(op=op):
                proc = self.run_op(op)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertTrue(
                    proc.stderr.startswith("ok "), proc.stderr)


# The end.
