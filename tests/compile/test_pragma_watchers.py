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
tests.compile.test_pragma_watchers

A pragma watcher is announced on rebuild for as long as something owns
it: an object watcher and a bound method live with their object, and a
plain callable lives with the pragma

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import gc
import unittest

import pytest

from koskari.lib import values
from koskari.pragma import ModulePragma


pytestmark = pytest.mark.phase_compile


class Receiver:

    def __init__(self, log):
        self.log = log

    def reload_pragma(self, pragma):
        self.log.append(("object", pragma))

    def on_pragma(self, pragma):
        self.log.append(("method", pragma))


def change():
    return ("compiler/cache-expression", values())


class TestPragmaWatchers(unittest.TestCase):

    def test_bound_method_watcher_is_announced(self):
        log = []
        receiver = Receiver(log)
        pragma = ModulePragma()
        pragma.watch(receiver.on_pragma)
        gc.collect()
        replacement = pragma.rebuild(change())
        self.assertEqual(log, [("method", replacement)])

    def test_plain_callable_watcher_is_announced(self):
        log = []
        pragma = ModulePragma()
        pragma.watch(lambda p: log.append(("lambda", p)))
        gc.collect()
        replacement = pragma.rebuild(change())
        self.assertEqual(log, [("lambda", replacement)])

    def test_object_watcher_is_announced_while_alive(self):
        log = []
        receiver = Receiver(log)
        pragma = ModulePragma()
        pragma.watch(receiver)
        replacement = pragma.rebuild(change())
        self.assertEqual(log, [("object", replacement)])

    def test_object_watcher_dies_with_its_object(self):
        log = []
        pragma = ModulePragma()
        pragma.watch(Receiver(log))
        gc.collect()
        pragma.rebuild(change())
        self.assertEqual(log, [])

    def test_bound_method_watcher_dies_with_its_receiver(self):
        log = []
        pragma = ModulePragma()
        pragma.watch(Receiver(log).on_pragma)
        gc.collect()
        pragma.rebuild(change())
        self.assertEqual(log, [])

    def test_watchers_follow_the_rebuilt_pragma(self):
        log = []
        pragma = ModulePragma()
        pragma.watch(lambda p: log.append(p))
        second = pragma.rebuild(change())
        third = second.rebuild(change())
        self.assertEqual(log, [second, third])


# The end.
