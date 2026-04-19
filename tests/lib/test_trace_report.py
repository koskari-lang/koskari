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
Tests for koskari.lib.get_trace_report, clear_trace_counters, and compile-time
tracing flags

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4 via Cursor
"""

import unittest

import koskari.lib as lib

import pytest

pytestmark = pytest.mark.phase_lib


class TestTraceReport(unittest.TestCase):
    """
    Shape and types of get_trace_report() dict
    """

    def test_get_trace_report_has_flags(self):
        """
        Report always includes flags for each compile-time trace feature
        """

        report = lib.get_trace_report()
        self.assertIsInstance(report, dict)
        self.assertIn("flags", report)
        flags = report["flags"]
        self.assertIsInstance(flags, dict)
        self.assertIn("freelist", flags)
        self.assertIn("intern", flags)
        self.assertIn("events", flags)
        self.assertIn("timing", flags)
        for k in ("freelist", "intern", "events", "timing"):
            self.assertIsInstance(flags[k], bool)

    def test_events_section_when_disabled(self):
        """
        Default builds omit events counts; flags.events is False
        """

        report = lib.get_trace_report()
        flags = report["flags"]
        if not flags.get("events"):
            self.assertNotIn(
                "events",
                report,
                "events key should be absent when KOS_TRACE_EVENTS is off",
            )

    def test_events_section_shape_when_enabled(self):
        """
        When built with KOS_TRACE_EVENTS=1, events dict has string keys and ints
        """

        report = lib.get_trace_report()
        flags = report["flags"]
        if not flags.get("events"):
            self.skipTest(
                "KOS_TRACE_EVENTS not enabled; rebuild with KOS_TRACE_EVENTS=1"
            )
        self.assertIn("events", report)
        ev = report["events"]
        self.assertIsInstance(ev, dict)
        for name, val in ev.items():
            self.assertIsInstance(name, str)
            self.assertIsInstance(val, int)
            self.assertGreaterEqual(val, 0)

    def test_expected_event_keys_when_enabled(self):
        """
        Documented counter names appear when event tracing is on
        """

        report = lib.get_trace_report()
        if not report["flags"].get("events"):
            self.skipTest(
                "KOS_TRACE_EVENTS not enabled; rebuild with KOS_TRACE_EVENTS=1"
            )
        ev = report["events"]
        expected = (
            "dyn_handle_enter",
            "dyn_unwind_step",
            "transfer_plan_build",
            "raise_local_walk_iter",
        )
        for key in expected:
            self.assertIn(key, ev, msg="missing counter %r" % (key,))

    def test_opcode_timing_absent_when_disabled(self):
        """
        Default builds omit opcode_timing; flags.timing is False
        """

        report = lib.get_trace_report()
        flags = report["flags"]
        if not flags.get("timing"):
            self.assertNotIn(
                "opcode_timing",
                report,
                "opcode_timing key should be absent when KOS_TRACE_TIMING is off",
            )
            self.assertNotIn(
                "opcode_timing_summary",
                report,
                "opcode_timing_summary key should be absent when "
                "KOS_TRACE_TIMING is off",
            )
            self.assertNotIn(
                "post_handler_timing",
                report,
                "post_handler_timing key should be absent when KOS_TRACE_TIMING is off",
            )
            self.assertNotIn(
                "post_handler_timing_summary",
                report,
                "post_handler_timing_summary key should be absent when "
                "KOS_TRACE_TIMING is off",
            )

    def test_opcode_timing_shape_when_enabled(self):
        """
        When built with KOS_TRACE_TIMING=1, opcode_timing entries have count,
        total_ns, avg_ns, share fields, and ratio_vs_mean; summary aggregates
        and top lists are present
        """

        report = lib.get_trace_report()
        if not report["flags"].get("timing"):
            self.skipTest(
                "KOS_TRACE_TIMING not enabled; rebuild with KOS_TRACE_TIMING=1"
            )
        self.assertIn("opcode_timing", report)
        self.assertIn("opcode_timing_summary", report)
        ot = report["opcode_timing"]
        self.assertIsInstance(ot, dict)
        for name, val in ot.items():
            self.assertIsInstance(name, str)
            self.assertIsInstance(val, dict)
            self.assertIn("count", val)
            self.assertIn("total_ns", val)
            self.assertIn("avg_ns", val)
            self.assertIn("share_of_time", val)
            self.assertIn("share_of_executions", val)
            self.assertIn("ratio_vs_mean", val)
            self.assertIsInstance(val["count"], int)
            self.assertIsInstance(val["total_ns"], int)
            self.assertIsInstance(val["avg_ns"], int)
            self.assertIsInstance(val["share_of_time"], float)
            self.assertIsInstance(val["share_of_executions"], float)
            self.assertIsInstance(val["ratio_vs_mean"], float)
            self.assertGreaterEqual(val["count"], 0)
            self.assertGreaterEqual(val["total_ns"], 0)
            self.assertGreaterEqual(val["avg_ns"], 0)

        sm = report["opcode_timing_summary"]
        self.assertIsInstance(sm, dict)
        self.assertIn("total_executions", sm)
        self.assertIn("total_ns", sm)
        self.assertIn("avg_ns_per_dispatch", sm)
        self.assertIn("opcode_count_nonzero", sm)
        self.assertIn("max_total_ns_op", sm)
        self.assertIn("max_total_ns", sm)
        self.assertIn("max_avg_ns_op", sm)
        self.assertIn("max_avg_ns", sm)
        self.assertIn("top_by_total_ns", sm)
        self.assertIn("top_by_avg_ns", sm)
        self.assertIsInstance(sm["total_executions"], int)
        self.assertIsInstance(sm["total_ns"], int)
        self.assertIsInstance(sm["avg_ns_per_dispatch"], int)
        self.assertIsInstance(sm["opcode_count_nonzero"], int)
        self.assertIsInstance(sm["max_total_ns_op"], str)
        self.assertIsInstance(sm["max_total_ns"], int)
        self.assertIsInstance(sm["max_avg_ns_op"], str)
        self.assertIsInstance(sm["max_avg_ns"], int)
        self.assertIsInstance(sm["top_by_total_ns"], list)
        self.assertIsInstance(sm["top_by_avg_ns"], list)
        self.assertLessEqual(len(sm["top_by_total_ns"]), 10)
        self.assertLessEqual(len(sm["top_by_avg_ns"]), 10)
        for row in sm["top_by_total_ns"]:
            self.assertIsInstance(row, dict)
            self.assertIn("op", row)
            self.assertIn("total_ns", row)
            self.assertIsInstance(row["op"], str)
            self.assertIsInstance(row["total_ns"], int)
        for row in sm["top_by_avg_ns"]:
            self.assertIsInstance(row, dict)
            self.assertIn("op", row)
            self.assertIn("avg_ns", row)
            self.assertIsInstance(row["op"], str)
            self.assertIsInstance(row["avg_ns"], int)

    def test_post_handler_timing_shape_when_enabled(self):
        """
        When built with KOS_TRACE_TIMING=1, post_handler_timing entries match
        opcode_timing field shape; summary includes rollup and top lists
        """

        report = lib.get_trace_report()
        if not report["flags"].get("timing"):
            self.skipTest(
                "KOS_TRACE_TIMING not enabled; rebuild with KOS_TRACE_TIMING=1"
            )
        self.assertIn("post_handler_timing", report)
        self.assertIn("post_handler_timing_summary", report)
        ph = report["post_handler_timing"]
        self.assertIsInstance(ph, dict)
        for name, val in ph.items():
            self.assertIsInstance(name, str)
            self.assertIsInstance(val, dict)
            self.assertIn("count", val)
            self.assertIn("total_ns", val)
            self.assertIn("avg_ns", val)
            self.assertIn("share_of_time", val)
            self.assertIn("share_of_executions", val)
            self.assertIn("ratio_vs_mean", val)
            self.assertIsInstance(val["count"], int)
            self.assertIsInstance(val["total_ns"], int)
            self.assertIsInstance(val["avg_ns"], int)
            self.assertIsInstance(val["share_of_time"], float)
            self.assertIsInstance(val["share_of_executions"], float)
            self.assertIsInstance(val["ratio_vs_mean"], float)
            self.assertGreaterEqual(val["count"], 0)
            self.assertGreaterEqual(val["total_ns"], 0)
            self.assertGreaterEqual(val["avg_ns"], 0)

        sm = report["post_handler_timing_summary"]
        self.assertIsInstance(sm, dict)
        self.assertIn("total_executions", sm)
        self.assertIn("total_ns", sm)
        self.assertIn("avg_ns_per_dispatch", sm)
        self.assertIn("handler_count_nonzero", sm)
        self.assertIn("max_total_ns_handler", sm)
        self.assertIn("max_total_ns", sm)
        self.assertIn("max_avg_ns_handler", sm)
        self.assertIn("max_avg_ns", sm)
        self.assertIn("rollup_invocations", sm)
        self.assertIn("rollup_total_ns", sm)
        self.assertIn("rollup_avg_ns", sm)
        self.assertIn("top_by_total_ns", sm)
        self.assertIn("top_by_avg_ns", sm)
        self.assertIsInstance(sm["rollup_invocations"], int)
        self.assertIsInstance(sm["rollup_total_ns"], int)
        self.assertIsInstance(sm["rollup_avg_ns"], int)
        self.assertLessEqual(len(sm["top_by_total_ns"]), 10)
        self.assertLessEqual(len(sm["top_by_avg_ns"]), 10)
        for row in sm["top_by_total_ns"]:
            self.assertIsInstance(row, dict)
            self.assertIn("handler", row)
            self.assertIn("total_ns", row)
            self.assertIsInstance(row["handler"], str)
            self.assertIsInstance(row["total_ns"], int)
        for row in sm["top_by_avg_ns"]:
            self.assertIsInstance(row, dict)
            self.assertIn("handler", row)
            self.assertIn("avg_ns", row)
            self.assertIsInstance(row["handler"], str)
            self.assertIsInstance(row["avg_ns"], int)

    def test_clear_trace_counters_returns_none(self):
        """
        clear_trace_counters is always available and returns None
        """

        self.assertIsNone(lib.clear_trace_counters())
        self.assertIsNone(lib.clear_trace_counters())

    def test_clear_trace_counters_resets_timing_when_enabled(self):
        """
        When KOS_TRACE_TIMING=1, clear zeros opcode_timing_summary totals
        """

        if not lib.get_trace_report()["flags"].get("timing"):
            self.skipTest(
                "KOS_TRACE_TIMING not enabled; rebuild with KOS_TRACE_TIMING=1"
            )
        lib.clear_trace_counters()
        report = lib.get_trace_report()
        sm = report["opcode_timing_summary"]
        self.assertEqual(sm["total_executions"], 0)
        self.assertEqual(sm["total_ns"], 0)
        self.assertEqual(sm["avg_ns_per_dispatch"], 0)
        self.assertEqual(sm["opcode_count_nonzero"], 0)
        self.assertEqual(report["opcode_timing"], {})
        phs = report["post_handler_timing_summary"]
        self.assertEqual(phs["total_executions"], 0)
        self.assertEqual(phs["total_ns"], 0)
        self.assertEqual(phs["avg_ns_per_dispatch"], 0)
        self.assertEqual(phs["handler_count_nonzero"], 0)
        self.assertEqual(phs["rollup_invocations"], 0)
        self.assertEqual(phs["rollup_total_ns"], 0)
        self.assertEqual(phs["rollup_avg_ns"], 0)
        self.assertEqual(report["post_handler_timing"], {})

    def test_clear_trace_counters_resets_events_when_enabled(self):
        """
        When KOS_TRACE_EVENTS=1, clear zeros all event counters
        """

        if not lib.get_trace_report()["flags"].get("events"):
            self.skipTest(
                "KOS_TRACE_EVENTS not enabled; rebuild with KOS_TRACE_EVENTS=1"
            )
        lib.clear_trace_counters()
        report = lib.get_trace_report()
        ev = report["events"]
        for val in ev.values():
            self.assertEqual(val, 0)


if __name__ == "__main__":
    unittest.main()
