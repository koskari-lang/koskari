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
The standalone matcher function against the engine over the corpus:
every form a record reads, and every sub-form of it, is matched by the
patterns of the special its head names, through the engine and through
the function compiled from the same frozen matcher, and the two must
agree on the match and on every capture.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from koskari.lib import Datum, DatumFrame
from koskari.lib import Matcher as EngineMatcher
from koskari.matcher import MatchCompiler, Pattern
from koskari.syntax import Syntax
from koskari.types import is_nil, is_pair, is_symbol
from tests.corpus import CorpusTestCase, load_corpus_index


pytestmark = pytest.mark.phase_corpus


def normal(value):
    if isinstance(value, DatumFrame):
        return ("frame", {k: normal(v) for k, v in value.items()},
                normal(value.source), value.position)
    if isinstance(value, Datum):
        return ("datum", normal(value.datum), value.position)
    if isinstance(value, dict):
        return {k: normal(v) for k, v in value.items()}
    if isinstance(value, list):
        return [normal(v) for v in value]
    if isinstance(value, tuple):
        return tuple(normal(v) for v in value)
    return value


def subforms(form, out):
    """
    Every pair reachable from form, form first; a cell is visited once
    """

    seen = set()
    stack = [form]
    while stack:
        cell = stack.pop()
        if isinstance(cell, Datum):
            cell = cell.datum
        if not is_pair(cell) or is_nil(cell) or id(cell) in seen:
            continue
        seen.add(id(cell))
        out.append(cell)
        stack.append(cell.car)
        stack.append(cell.cdr)
    return out


class Oracle:
    """
    The native engine as the oracle: one engine matcher per transform,
    thawed from the same frozen tuple the transform's function was
    compiled from. Goes with the engine when it is removed
    """

    def __init__(self):
        self.engines = {}
        self.compared = 0
        self.matched = 0

    def engine(self, transform):
        found = self.engines.get(id(transform))
        if found is None:
            frozen = MatchCompiler().lower(transform.pattern)
            found = EngineMatcher.from_tuple(frozen)
            self.engines[id(transform)] = found
        return found

    def compare(self, transform, form):
        try:
            want = self.engine(transform).match(form)
        except TypeError:
            # an extern the defaults do not cover; the engine's own
            # refusal, not a shape to compare
            return
        got = transform.matcher.match(form)
        self.compared += 1
        if not want.matched:
            assert not got.matched, (transform.pattern, form)
            return
        self.matched += 1
        assert got.matched, (transform.pattern, form)
        assert normal(got.captures) == normal(want.captures), (
            transform.pattern, form)


def test_every_special_pattern_agrees_over_the_corpus():
    index = load_corpus_index()
    oracle = Oracle()
    case = CorpusTestCase()
    case.setUp()
    forms = []
    for record in index.values():
        case.bind_record(record)
        try:
            form = case.read_source(record['source'])
        except Exception:
            # a record whose source does not read is a reader case
            continue
        subforms(form, forms)

    for form in forms:
        head = form.car
        if isinstance(head, Datum):
            head = head.datum
        if not is_symbol(head):
            continue
        special = case.compiler.find_compiled(head)
        if not isinstance(special, Syntax):
            continue
        for transform in special.transforms:
            # a transform a module built carries its pattern's source,
            # not a Pattern; the engine cannot be thawed for it
            if not isinstance(getattr(transform, "pattern", None), Pattern):
                continue
            oracle.compare(transform, form)

    print(
        f"oracle: {len(forms)} forms, {oracle.compared} comparisons,"
        f" {oracle.matched} matches, {len(oracle.engines)} matchers")
    assert oracle.compared > 1000, oracle.compared
    assert oracle.matched > 100, oracle.matched


# The end.
