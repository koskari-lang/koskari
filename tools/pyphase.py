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
pyphase - run pytest with phase markers

Runs pytest with phase markers, so that tests are run in phases

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys
import time
from dataclasses import dataclass

import pytest


@dataclass(frozen=True)
class Phase:
    name: str
    marker: str


class PhaseRunnerPlugin:

    def __init__(self):
        self.phases = []
        self.phase_index = {}
        self.phase_by_marker = {}

        self.current_phase_marker = None
        self.failed_phase_marker = None
        self.omitted_count = 0

        self.phase_seen = set()
        self.phase_failed = set()

        self.count_passed = 0
        self.count_failed = 0
        self.count_skipped = 0
        self.count_xfailed = 0
        self.count_xpassed = 0
        self.count_error = 0

        self._counted_nodeids = set()
        self.start_time = time.perf_counter()


    def pytest_configure(self, config):
        raw = config.getini("markers")

        self.phases = phases = load_phases(config)
        self.phase_index = {p.marker: i for i, p in enumerate(phases)}
        self.phase_by_marker = {p.marker: p for p in phases}


    def _marker_for_item(self, item):
        for phase in self.phases:
            if item.get_closest_marker(phase.marker):
                return phase.marker
        return None


    def _phase_name(self, marker):
        if marker is None:
            return "unphased"
        return self.phase_by_marker[marker].name


    def _terminal(self, config):
        return config.pluginmanager.get_plugin("terminalreporter")


    def _write_phase_summary(self, terminalreporter, config):
        elapsed = time.perf_counter() - self.start_time

        terminalreporter.write_sep("=", "PHASE SUMMARY", bold=True)

        blocked = self.failed_phase_marker is not None
        failed_idx = (
            self.phase_index[self.failed_phase_marker]
            if self.failed_phase_marker is not None
            else None
        )

        for phase in self.phases:
            idx = self.phase_index[phase.marker]
            if phase.marker in self.phase_failed:
                status = "FAILED"
            elif blocked and failed_idx is not None and idx > failed_idx:
                status = "OMITTED"
            elif phase.marker in self.phase_seen:
                status = "PASSED"
            else:
                status = "NOT RUN"

            terminalreporter.write_line(f"{phase.name:<12} {status:<8} ({phase.marker})")

        terminalreporter.write_sep("-", "TOTALS")
        terminalreporter.write_line(f"passed:   {self.count_passed}")
        terminalreporter.write_line(f"failed:   {self.count_failed}")
        terminalreporter.write_line(f"errors:   {self.count_error}")
        if self.count_skipped:
            terminalreporter.write_line(f"skipped:  {self.count_skipped}")
        if self.count_xfailed:
            terminalreporter.write_line(f"xfailed:  {self.count_xfailed}")
        if self.count_xpassed:
            terminalreporter.write_line(f"xpassed:  {self.count_xpassed}")
        terminalreporter.write_line(f"omitted:  {self.omitted_count}")
        terminalreporter.write_line(f"duration: {elapsed:.2f}s")


    def _write_phase_header(self, config, marker):
        tr = self._terminal(config)
        if tr is None:
            return

        idx = self.phase_index[marker] + 1
        total = len(self.phases)
        phase = self.phase_by_marker[marker]
        tr.write_sep("=", f"PHASE {idx}/{total}: {phase.name} ({phase.marker})", bold=True)


    def pytest_collection_modifyitems(self, session, config, items):
        def sort_key(item):
            marker = self._marker_for_item(item)
            if marker is None:
                return (len(self.phases), item.nodeid)
            return (self.phase_index[marker], item.nodeid)

        items.sort(key=sort_key)


    def pytest_runtest_protocol(self, item, nextitem):
        marker = self._marker_for_item(item)

        # Unmarked tests run last by default. You can change this to fail fast instead.
        if marker is None:
            return None

        if marker != self.current_phase_marker:
            self.current_phase_marker = marker
            self.phase_seen.add(marker)
            self._write_phase_header(item.config, marker)

        if self.failed_phase_marker is not None:
            current_idx = self.phase_index[marker]
            failed_idx = self.phase_index[self.failed_phase_marker]
            if current_idx > failed_idx:
                self.omitted_count += 1
                return True  # handled: omit this test entirely, no skip/deselect noise

        return None  # let pytest run the item normally


    def pytest_runtest_logreport(self, report):
        marker = None
        for phase in self.phases:
            if phase.marker in report.keywords:
                marker = phase.marker
                break

        # Mark the phase as failed on any actual test failure/error.
        if marker is not None and report.failed:
            self.phase_failed.add(marker)
            if self.failed_phase_marker is None:
                self.failed_phase_marker = marker

        # Count each nodeid once for final summary.
        if report.nodeid in self._counted_nodeids:
            return

        # Normal tests: count on call.
        if report.when == "call":
            self._counted_nodeids.add(report.nodeid)
            if report.passed:
                self.count_passed += 1
            elif report.failed:
                self.count_failed += 1
            elif report.skipped:
                if hasattr(report, "wasxfail"):
                    self.count_xfailed += 1
                else:
                    self.count_skipped += 1
            return

        # setup/teardown failures: count as error once.
        if report.when in {"setup", "teardown"} and report.failed:
            self._counted_nodeids.add(report.nodeid)
            self.count_error += 1


    def pytest_terminal_summary(self, terminalreporter, exitstatus, config):
        self._write_phase_summary(terminalreporter, config)
        terminalreporter.summary_warnings()


    def pytest_sessionfinish(self, session, exitstatus):
        summary_exit_codes = (0, 1, 2, 4, 5)
        exitval = getattr(exitstatus, "value", exitstatus)
        if exitval not in summary_exit_codes:
            return
        if not getattr(session.config.option, "no_summary", False):
            return
        tr = session.config.pluginmanager.get_plugin("terminalreporter")
        if tr is None:
            return
        tr._tw.line("")
        tr.summary_errors()
        tr.summary_failures()
        tr.summary_warnings()


def load_phases(config):
    raw = config.getini("markers")

    phases = []

    for line in raw:
        marker, *rest = line.split(":", 1)

        marker = marker.strip()
        desc = rest[0].strip() if rest else marker

        if marker.startswith("phase_"):
            phases.append(Phase(desc, marker))

    return phases


def main():
    plugin = PhaseRunnerPlugin()

    # --no-summary suppresses pytest's built-in final summary line,
    # so you won't get the usual "... deselected ..." / default totals.
    return pytest.main(
        [
            "--no-summary",
            "tests/",
        ],
        plugins=[plugin],
    )


if __name__ == "__main__":
    sys.exit(main())


# The end.
