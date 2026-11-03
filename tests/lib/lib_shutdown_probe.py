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
A probe run as its own process by tests/lib/test_lib_shutdown.py: a
finalizer that runs in the interpreter's last collection, after the
module object of koskari.lib has been collected with the other modules,
and uses a static type through a reference of its own. Only type objects
are bound, never a module-level builtin function, since a builtin
function keeps its module alive. The operation named on the command
line is run from the finalizer and its outcome is written to stderr as
"ok REPR" or "raised REPR".

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import contextvars
import os
import sys

import koskari.lib as kl


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


class Holder:
    """
    A self-referencing object whose finalizer runs the probed operation
    in the interpreter's last collection
    """

    def __init__(self, fn):
        self.me = self
        self.fn = fn

    def __del__(self, write=os.write, K=K):
        try:
            result = self.fn()
        except K['BaseException'] as exc:
            write(2, ("raised " + K['repr'](exc)).encode())
        else:
            write(2, ("ok " + K['repr'](result)).encode())


if __name__ == "__main__":
    FN = globals()["op_" + sys.argv[1]]
    VAR = contextvars.ContextVar("holder")
    VAR.set(Holder(FN))
    del kl


# The end.
