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
Result records and presentation for the churn harness.

Measurement lives in :mod:`churn`; this module only records and renders it.
``format_result`` produces the human line, ``to_json`` the machine-readable
metrics payload for tracking across commits.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import dataclasses
import statistics
import typing


@dataclasses.dataclass
class Timing:
    """
    The measured samples for one variant (``python`` or ``koskari``) of a
    benchmark.  Each sample is the wall time for ``runs`` calls; ``best`` (the
    minimum) is the stable signal for a compute-bound microbenchmark.
    """

    label: str
    runs: int
    samples: typing.List[float]
    answer: object

    @property
    def best(self):
        return min(self.samples)

    @property
    def median(self):
        return statistics.median(self.samples)

    @property
    def total(self):
        return sum(self.samples)

    @property
    def per_call(self):
        return self.best / self.runs


@dataclasses.dataclass
class Result:
    """
    The outcome of running one benchmark in one mode: the per-variant
    timings, whether the answers agreed with each other and any known
    ``expected`` value, and a note for skipped cases.
    """

    name: str
    mode: str
    tags: typing.Tuple
    expected: object
    timings: typing.List[Timing]
    ok: bool
    note: str = ""

    @property
    def ratio(self):
        """
        koskari-best / python-best when both variants ran, else None.
        A value above 1.0 means Koskari is that many times slower.
        """

        by = {t.label: t for t in self.timings}
        if "python" in by and "koskari" in by and by["python"].best:
            return by["koskari"].best / by["python"].best
        return None


# Column layout for the human-readable table: (heading, align, width).
# Labels live in the header row, so the data rows carry only values.
_COLUMNS = (
    ("benchmark", "<", 14),
    ("py-best", ">", 10),
    ("py-med", ">", 10),
    ("ki-best", ">", 10),
    ("ki-med", ">", 10),
    ("ki/py", ">", 7),
)


def _fmt_time(t):
    if t is None:
        return ""
    return "%.4fs" % t


def _cells(texts):
    return " ".join(
        format(text, align + str(width))
        for text, (_, align, width) in zip(texts, _COLUMNS))


def format_header():
    """
    Render the table heading row plus an underline rule.
    """

    head = _cells(name for name, _, _ in _COLUMNS)
    return head + "\n" + ("-" * len(head))


def format_result(result):
    """
    Render one result as a single table row, aligned under
    :func:`format_header`.
    """

    if result.note:
        return format(result.name, "<14") + " " + result.note

    by = {t.label: t for t in result.timings}
    py = by.get("python")
    ki = by.get("koskari")
    ratio = result.ratio

    row = _cells((
        result.name,
        _fmt_time(py.best) if py else "",
        _fmt_time(py.median) if py else "",
        _fmt_time(ki.best) if ki else "",
        _fmt_time(ki.median) if ki else "",
        ("%.2fx" % ratio) if ratio is not None else "",
    ))
    row = row.rstrip()
    if not result.ok:
        row += "  !! MISMATCH"
    return row


def _timing_json(t):
    return {
        "runs": t.runs,
        "best": t.best,
        "median": t.median,
        "total": t.total,
        "per_call": t.per_call,
        "answer": t.answer,
    }


def _result_json(result):
    return {
        "name": result.name,
        "tags": list(result.tags),
        "ok": result.ok,
        "note": result.note,
        "expected": result.expected,
        "ratio_koskari_over_python": result.ratio,
        "variants": {t.label: _timing_json(t) for t in result.timings},
    }


def to_json(results, mode, samples, warmup):
    """
    Build the serializable metrics payload for a run.  Dump it with
    ``json.dump(..., indent=2, default=str)`` so non-JSON answer values
    (tuples, floats) survive.
    """

    return {
        "mode": mode,
        "samples": samples,
        "warmup": warmup,
        "results": [_result_json(r) for r in results],
    }


# The end.
