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
tests.lib.test_match_root_gate_once

A sequence or mapping arm decides whether a subject descends with one
gate test. On 3.9 a subject that is neither a builtin sequence nor a
dict is gated through the collections ABC, which reads the subject's
__class__; that read happens once per arm. From 3.10 the gate is a type
flag and reads nothing.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import unittest
from collections.abc import Mapping, Sequence

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
define pair_of [v]:
    match v:
        case #[a b]:
            (+ a b)
        else:
            "miss"

define keyed [v]:
    match v:
        case #{"k": x}:
            x
        else:
            "miss"
"""


EXPECTED_READS = 1 if sys.version_info < (3, 10) else 0


class TestMatchRootGateOnce(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("match_root_gate_once")
        vars(module)["__file__"] = "<match_root_gate_once>"
        await load_module_source_async(module, SRC)
        return vars(module)

    async def test_sequence_gate_reads_class_once_per_arm(self):
        reads = []

        class Seq:
            def __init__(self, *items):
                self.items = items

            def __len__(self):
                return len(self.items)

            def __getitem__(self, i):
                return self.items[i]

            @property
            def __class__(self):
                reads.append(1)
                return Seq

        Sequence.register(Seq)

        mod = await self.load()
        self.assertEqual(await mod["pair_of"](Seq(2, 3)), 5)
        self.assertEqual(len(reads), EXPECTED_READS)

    async def test_mapping_gate_reads_class_once_per_arm(self):
        reads = []

        class Map:
            def __init__(self, **items):
                self.items = items

            def __len__(self):
                return len(self.items)

            def __iter__(self):
                return iter(self.items)

            def __getitem__(self, k):
                return self.items[k]

            def get(self, k, default=None):
                return self.items.get(k, default)

            @property
            def __class__(self):
                reads.append(1)
                return Map

        Mapping.register(Map)

        mod = await self.load()
        self.assertEqual(await mod["keyed"](Map(k=9)), 9)
        self.assertEqual(len(reads), EXPECTED_READS)


# The end.
