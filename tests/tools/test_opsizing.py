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
Unit tests for tools.opsizing

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest


import koskari  # noqa: E402,F401  (activates the .ki import hook)

from tools import opsizing  # noqa: E402


NM_TEXT = """\
000000000007e7e0 00000000000000d4 t op_add
000000000004ca00 00000000000000b2 t op_ATTR_CLEAR.lto_priv.0
000000000004cab2 0000000000000010 t op_ATTR_CLEAR.lto_priv.0.cold
000000000004e350 00000000000001a1 t op_CONST
0000000000010000 0000000000000020 T koskari_eval
000000000004f000 0000000000000030 t object_next_step
"""


TIMING = {
    'CONST': {
        'count': 10, 'avg_ns': 12, 'total_ns': 120,
        'share_of_time': 0.5, 'share_of_executions': 0.25},
    'trampoline': {
        'count': 2, 'avg_ns': 300, 'total_ns': 600,
        'share_of_time': 0.1, 'share_of_executions': 0.01},
}


class TestParseSizes(unittest.TestCase):

    def test_folds_suffixes_and_skips_non_handlers(self):
        sizes = opsizing.parse_sizes(NM_TEXT)
        self.assertEqual(sizes, {'ATTR_CLEAR': 0xb2 + 0x10, 'CONST': 0x1a1})


class TestJoinRows(unittest.TestCase):

    def test_rows_cover_every_opcode_and_reported_pseudo_slots(self):
        rows = opsizing.join_rows(
            ['POP', 'CONST'], {'CONST': 417, 'POP': 40}, TIMING, 7.5)
        self.assertEqual(
            [row['opcode'] for row in rows], ['CONST', 'POP', 'trampoline'])

    def test_net_subtracts_the_step_overhead(self):
        rows = opsizing.join_rows(['CONST'], {'CONST': 417}, TIMING, 7.5)
        const = rows[0]
        self.assertEqual(const['bytes'], 417)
        self.assertEqual(const['count'], 10)
        self.assertEqual(const['avg_ns'], 12)
        self.assertEqual(const['net_ns'], 4.5)
        self.assertEqual(const['time_pct'], 50.0)

    def test_unreported_opcode_keeps_its_size_only(self):
        rows = opsizing.join_rows(['POP'], {'POP': 40}, TIMING, 7.5)
        pop = [row for row in rows if row['opcode'] == 'POP'][0]
        self.assertEqual(pop['bytes'], 40)
        self.assertIsNone(pop['count'])
        self.assertIsNone(pop['net_ns'])

    def test_pseudo_slot_has_no_size(self):
        rows = opsizing.join_rows([], {}, TIMING, 0)
        self.assertEqual([row['opcode'] for row in rows], ['trampoline'])
        self.assertIsNone(rows[0]['bytes'])
        self.assertEqual(rows[0]['net_ns'], 300)


class TestRenderTsv(unittest.TestCase):

    def test_meta_header_and_cells(self):
        rows = opsizing.join_rows(['CONST', 'POP'], {'POP': 40}, TIMING, 7.5)
        text = opsizing.render_tsv(rows, [('sha', 'abc1234')])
        lines = text.splitlines()
        self.assertEqual(lines[0], '# sha: abc1234')
        self.assertEqual(lines[1].split('\t'), list(opsizing.COLUMNS))
        self.assertEqual(
            lines[2].split('\t'),
            ['CONST', '', '10', '12', '4.500', '120', '50.000', '25.000'])
        self.assertEqual(
            lines[3].split('\t'), ['POP', '40', '', '', '', '', '', ''])
        self.assertTrue(text.endswith('\n'))


# The end.
