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
koskari.syntax

Syntax-domain API: Syntax boxing, transformer models, and runtime
tagged-tuple builders (including deserialize paths for compiled macros).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from dataclasses import dataclass
from typing import Any

from .hygiene import (
    apply_syntax_rules_hygiene,
    collect_binding_symbols,
    expand_template_with_hygiene,
)
from .lib import car, cdr, cons, demand_call, nil, symbol
from .matcher import (LITERAL, MATCH, Matcher, RepeatResult, match)
from .matcher import from_tuple as matcher_from_tuple
from .types import (build_proper, get_position, is_nil, is_pair,
                    is_proper, unpack)


__all__ = (
    'MacroRule',
    'MacroRulesSegment',
    'MacroRulesTransformer',
    'SpecialRule',
    'SpecialRulesTransformer',
    'Syntax',
    'SyntaxRule',
    'SyntaxRulesSegment',
    'SyntaxRulesTransformer',
    'box_match_capture',
    'build_macro_syntax',
    'build_special_match',
    'build_special_syntax',
    'build_syntax',
    'unbox_macro',
    'unbox_macro_with_hygiene',
)


_MATCH_STRUCT_TYPE = type(MATCH(LITERAL(symbol('_'))))


@dataclass(frozen=True)
class Syntax:
    """
    Wraps a Koskari datum with optional source position and optional tag.

    :param datum: underlying form (pair, symbol, nil, literals)
    :param position: source position tuple or None when unknown
    :param tag: optional interpretation hint; reserved for future use

    Iteration is defined when ``datum`` is a pair (including empty list
    ``nil``): each step yields a ``Syntax`` box around one element of
    ``unpack(datum)``, using the cons cell's position when set. Non-pair
    datums raise ``TypeError`` (for example when treating a box as a splice
    source).
    """

    datum: Any
    position: Any = None
    tag: Any = None


    def __iter__(self):
        """
        Yield ``Syntax`` values for each element from the pair spine.

        :raises TypeError: if ``datum`` is not a pair
        :return: iterator of Syntax instances
        """

        d = self.datum

        # TODO: I might want to allow iteration over list or tuple in case
        # we ever leak those into source forms. Not for now though.
        if not is_pair(d):
            raise TypeError(
                f"cannot iterate Syntax with non-pair datum: {d}")

        cell = d
        for elem in unpack(d):
            if is_pair(cell) and not is_nil(cell):
                pos = get_position(cell) or self.position
                yield Syntax(elem, pos, self.tag)
                cell = cdr(cell)
            else:
                yield Syntax(elem, self.position, self.tag)


    @property
    def car(self):
        return self.datum.car


    @property
    def cdr(self):
        return self.datum.cdr


def box_match_capture(value, position_fallback=None):
    """
    Build a Syntax value from a simple capture, or box each repetition for
    RepeatResult.

    :param value: MatchResult (or subclass) or RepeatResult from matching
    :param position_fallback: used when source_pos is unset and the datum is
        not a pair with get_position (simple captures only)
    :return: Syntax instance, or list of per-iteration dicts for RepeatResult,
        or None if a nested RepeatResult boxing fails
    """

    if isinstance(value, RepeatResult):
        rows = []
        for sub_res, _specs in value.matches:
            flat = {}
            for k, v in sub_res.items():
                boxed = box_match_capture(v, position_fallback)
                if boxed is None:
                    return None
                flat[symbol(k)] = boxed
            rows.append(flat)

        return rows

    d = value.source
    pos = value.source_pos
    if pos is None and is_pair(d):
        pos = get_position(d)
    if pos is None:
        pos = position_fallback

    return Syntax(d, pos)


_tag_syntax_rules = symbol("syntax/rules")
_tag_list = symbol("syntax/list")
_tag_cons = symbol("syntax/cons")


def _serialize_form(form):
    """
    Encode a Koskari form as tagged tuples for const-pool persistence

    Proper lists use ``syntax/list``; improper pairs use ``syntax/cons``;
    tuples and frozensets recurse structurally; atoms pass through

    :param form: pair tree, tuple, frozenset, or leaf datum
    :return: tagged tuple, nil, or leaf suitable for ``from_tuple`` round-trip
    """

    if form is nil:
        return nil

    if is_pair(form):
        if is_proper(form):
            return (_tag_list,) + tuple(_serialize_form(x) for x in unpack(form))
        return (_tag_cons, _serialize_form(car(form)), _serialize_form(cdr(form)))

    if isinstance(form, tuple):
        return tuple(_serialize_form(x) for x in form)

    if isinstance(form, frozenset):
        return frozenset(_serialize_form(x) for x in form)

    return form


def _deserialize_form(payload):
    """
    Decode :func:`_serialize_form` output back to Koskari pairs and datums

    :param payload: nil, tagged tuple from serialization, or pass-through leaf
    :raises ValueError: when a ``syntax/cons`` payload has wrong length
    :return: cons tree, tuple, frozenset, or leaf
    """

    if payload is nil:
        return nil

    if isinstance(payload, tuple) and payload:
        tag = payload[0]

        if tag is _tag_list:
            return build_proper(*(_deserialize_form(x) for x in payload[1:]))

        if tag is _tag_cons:
            if len(payload) != 3:
                raise ValueError("invalid cons payload")
            return cons(_deserialize_form(payload[1]), _deserialize_form(payload[2]))

        return tuple(_deserialize_form(x) for x in payload)

    if isinstance(payload, frozenset):
        return frozenset(_deserialize_form(x) for x in payload)

    return payload


def _match_dict_to_syntax_kwargs(raw, position_fallback=None):
    """
    Box macro-rules match results into ``Syntax`` keyword arguments

    Each capture becomes a :class:`Syntax` wrapper (or None on boxing failure).
    Used when invoking procedural macro lambdas

    :param raw: dict from :func:`~koskari.matcher.match` (string keys)
    :param position_fallback: default position when captures lack one
    :return: dict mapping symbol keys to ``Syntax``, or None if boxing failed
    """

    if not raw:
        return {}

    out = {}
    for k, v in raw.items():
        boxed = box_match_capture(v, position_fallback)
        if boxed is None:
            return None

        out[symbol(k)] = boxed

    return out


def _merge_bindings(dst, src):
    """
    Merge ``src`` binding entries into ``dst`` for macro-rules environments

    Keys must agree when both sides bind the same name; list-valued repeats
    must match exactly between merges

    :param dst: target dict (mutated)
    :param src: new bindings to fold in
    :return: True if merge succeeded, False on conflict
    """

    for k, v in src.items():
        if k in dst:
            cur = dst[k]
            if isinstance(cur, list) or isinstance(v, list):
                if not isinstance(cur, list) or not isinstance(v, list):
                    return False
                if cur != v:
                    return False
            elif cur != v:
                return False
        else:
            dst[k] = v

    return True


def _syntax_match_result_to_bindings(raw):
    """
    Turn a syntax-rules match dict into plain symbol-to-datum bindings

    Flattens :func:`box_match_capture` output: simple captures become one
    entry per key; ``RepeatResult`` rows merge into list-valued keys

    :param raw: dict from :func:`~koskari.matcher.match`
    :return: bindings dict suitable for :func:`apply_syntax_rules_hygiene`, or
        None if repeat merging conflicts or boxing fails
    """

    if not raw:
        return {}

    out = {}

    for k, v in raw.items():
        boxed = box_match_capture(v)
        if boxed is None:
            return None

        if isinstance(boxed, list):
            merged = {}
            for item in boxed:
                for rk, val in item.items():
                    merged.setdefault(rk, []).append(val.datum)

            if not _merge_bindings(out, merged):
                return None

        else:
            if not _merge_bindings(out, {symbol(k): boxed.datum}):
                return None

    return out


def _unwrap_repeat_macro_rows(val):
    """
    Turn :func:`box_match_capture` output for ``RepeatResult`` into a flat list

    After ``box_match_capture``, a repeat region is a list of per-iteration
    dicts with one key each (the inner pattern variable). Macro bodies expect
    a list of ``Syntax`` values for ``(apply #list xs)``-style expansions.

    :param val: keyword value (possibly list of one-key dicts)
    :return: list of boxed captures, or ``val`` if not repeat-shaped
    """

    if not isinstance(val, list) or not val:
        return val

    if not all(isinstance(row, dict) and len(row) == 1 for row in val):
        return val

    keys0 = next(iter(val[0]))
    for row in val:
        if next(iter(row)) != keys0:
            return val

    return [row[keys0] for row in val]


@dataclass(frozen=True)
class SyntaxRule:
    pattern_matcher: Matcher
    template: object

    def to_tuple(self):
        return (self.pattern_matcher.to_tuple(), _serialize_form(self.template))

    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 2:
            raise ValueError("invalid syntax rule tuple")
        return cls(matcher_from_tuple(tup[0]), _deserialize_form(tup[1]))

    def try_expand(self, compiler, source_obj, literals):
        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        bindings = _syntax_match_result_to_bindings(raw)
        if bindings is None:
            return None

        return apply_syntax_rules_hygiene(
            self.template, bindings, compiler.find_compiled)


@dataclass(frozen=True)
class SyntaxRulesSegment:
    literals: frozenset
    rules: tuple


class SyntaxRulesTransformer:

    def __init__(self, name, literals, rules):
        self.name = name
        self.segments = (
            SyntaxRulesSegment(frozenset(literals), tuple(rules)),
        )


    @classmethod
    def from_segments(cls, name, segments):
        inst = cls.__new__(cls)
        inst.name = name
        inst.segments = tuple(segments)
        return inst


    def to_tuple(self):
        if len(self.segments) != 1:
            raise ValueError(
                "SyntaxRulesTransformer.to_tuple requires exactly one "
                "segment until multi-segment const-pool format exists")

        seg = self.segments[0]
        return (
            _tag_syntax_rules,
            _serialize_form(self.name),
            tuple(_serialize_form(x) for x in seg.literals),
            tuple(r.to_tuple() for r in seg.rules),
        )


    @classmethod
    def from_tuple(cls, tup):
        if not isinstance(tup, tuple) or len(tup) != 4 or tup[0] is not _tag_syntax_rules:
            raise ValueError("invalid syntax-rules tuple")

        name = _deserialize_form(tup[1])
        literals = frozenset(_deserialize_form(x) for x in tup[2])
        rules = tuple(SyntaxRule.from_tuple(rt) for rt in tup[3])
        return cls.from_segments(name, (SyntaxRulesSegment(literals, rules),))


    def compile_impl(self, compiler, source_obj, tc=False):
        for segment in self.segments:
            for rule in segment.rules:
                expanded = rule.try_expand(
                    compiler, source_obj, segment.literals)
                if expanded is not None:
                    return expanded

        raise ValueError(f"no matching define-syntax rule for {source_obj}")


def unbox_macro(expanded):
    """
    Strip Syntax wrappers from procedural macro output without hygiene

    :param expanded: form returned by the macro procedure (may contain ``Syntax``)
    :return: form ready for compilation (no ``Syntax`` wrappers)

    Unboxing updates ``car``/``cdr`` in place on existing pairs so source
    positions on those cells are preserved
    """

    seen = set()

    def walk(x):
        if isinstance(x, Syntax):
            return walk(x.datum)

        if is_nil(x):
            return x

        if is_pair(x):
            nx = x
            while nx is not nil:
                xi = id(nx)
                if xi in seen:
                    nx = nx.cdr
                    continue
                seen.add(xi)

                head, tail = nx
                if isinstance(head, Syntax):
                    nx.car = walk(head.datum)
                    nx.position = head.position
                elif is_nil(head):
                    pass
                elif is_pair(head):
                    walk(head)
                nx = tail
            return x
        return x

    return walk(expanded)


def unbox_macro_with_hygiene(expanded, find_compiled):
    """
    Run syntax-rules hygiene on procedural macro output, then strip Syntax

    Hygiene runs while ``Syntax`` boxes are still present so
    ``collect_binding_symbols`` and ``expand_template_with_hygiene`` never
    descend into boxed captures. Unboxing runs last for the compiler.

    :param expanded: form returned by the macro procedure (may contain ``Syntax``)
    :param find_compiled: callback used by syntax-rules to resolve special heads
    :return: form ready for compilation (no ``Syntax`` wrappers)

    Unboxing updates ``car``/``cdr`` in place on existing pairs so source
    positions on those cells are preserved
    """

    syms = collect_binding_symbols(expanded, find_compiled)
    hygiened = expand_template_with_hygiene(expanded, {}, syms, {})

    seen = set()

    def walk(x):
        # xi = id(x)
        # if xi in seen:
        #     return x

        if isinstance(x, Syntax):
            return walk(x.datum)

        if is_nil(x):
            return x

        if is_pair(x):
            nx = x
            while nx is not nil:
                xi = id(nx)
                if xi in seen:
                    nx = nx.cdr
                    continue
                seen.add(xi)

                head, tail = nx
                if isinstance(head, Syntax):
                    nx.car = walk(head.datum)
                    nx.position = head.position
                elif is_nil(head):
                    pass
                elif is_pair(head):
                    walk(head)
                nx = tail
            return x
        return x

    return walk(hygiened)


@dataclass
class MacroRule:
    pattern_matcher: Matcher
    procedure: Any = None


    def expand(self, source_obj, literals):
        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        if self.procedure is None:
            raise ValueError("internal: macro rule missing procedure")

        pos_fb = get_position(source_obj) if is_pair(source_obj) else None

        kwargs = _match_dict_to_syntax_kwargs(raw, position_fallback=pos_fb)
        if kwargs is None:
            raise ValueError("macro-rules: failed to box match captures")

        kwargs = {
            str(k): _unwrap_repeat_macro_rows(v) for k, v in kwargs.items()}

        return demand_call(self.procedure, **kwargs)


@dataclass
class SpecialRule:
    pattern_matcher: Matcher
    procedure: Any = None


    def expand(self, compiler, source_obj, literals, tc=False):
        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        if self.procedure is None:
            raise ValueError("internal: special rule missing procedure")

        pos_fb = get_position(source_obj) if is_pair(source_obj) else None

        kwargs = _match_dict_to_syntax_kwargs(raw, position_fallback=pos_fb)
        if kwargs is None:
            raise ValueError("special-rules: failed to box match captures")

        kwargs = {
            str(k): _unwrap_repeat_macro_rows(v) for k, v in kwargs.items()}

        return demand_call(self.procedure, compiler, tc, **kwargs)


@dataclass(frozen=True)
class MacroRulesSegment:
    literals: frozenset
    rules: tuple


class MacroRulesTransformer:

    def __init__(self, name, literals, rules):
        self.name = name
        self.segments = (
            MacroRulesSegment(frozenset(literals), tuple(rules)),
        )


    @classmethod
    def from_segments(cls, name, segments):
        inst = cls.__new__(cls)
        inst.name = name
        inst.segments = tuple(segments)
        return inst


    def compile_impl(self, compiler, source_obj, tc=False):
        for segment in self.segments:
            for rule in segment.rules:
                expanded = rule.expand(source_obj, segment.literals)
                if expanded is not None:
                    return unbox_macro_with_hygiene(
                        expanded, compiler.find_compiled)

        raise ValueError("no matching macro-rules rule")


class SpecialRulesTransformer:

    def __init__(self, name, literals, rules):
        self.name = name
        self.segments = (
            MacroRulesSegment(frozenset(literals), tuple(rules)),
        )


    @classmethod
    def from_segments(cls, name, segments):
        inst = cls.__new__(cls)
        inst.name = name
        inst.segments = tuple(segments)
        return inst


    def compile_impl(self, compiler, source_obj, tc=False):
        for segment in self.segments:
            for rule in segment.rules:
                expanded = rule.expand(
                    compiler, source_obj, segment.literals, tc=tc)
                if expanded is not None:
                    return unbox_macro(expanded)

        raise ValueError("no matching special-rules rule")


def build_syntax(name, tagged_tuple):
    transformer = SyntaxRulesTransformer.from_tuple(tagged_tuple)
    if name is not None:
        transformer.name = name
    return transformer


def build_macro_syntax(name, literals_tuple, *pattern_and_lambdas):
    """
    Build a MacroRulesTransformer from compile-time lowered arguments: name,
    serialized literals tuple, then alternating (pattern_tuple, fn) pairs.
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise ValueError("build-macro-rules requires pattern/lambda pairs")

    literals = frozenset(_deserialize_form(x) for x in literals_tuple)
    rules = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pattern_tup = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        pm = matcher_from_tuple(pattern_tup)
        rules.append(MacroRule(pm, procedure=fn))

    transformer = MacroRulesTransformer(name, literals, rules)
    return transformer


def build_special_syntax(name, literals_tuple, *pattern_and_lambdas):
    """
    Build a SpecialRulesTransformer from compile-time lowered arguments: name,
    serialized literals tuple, then alternating (pattern_tuple, fn) pairs.
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise ValueError("build-special-rules requires pattern/lambda pairs")

    literals = frozenset(_deserialize_form(x) for x in literals_tuple)
    rules = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pattern_tup = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        pm = matcher_from_tuple(pattern_tup)
        rules.append(SpecialRule(pm, procedure=fn))

    transformer = SpecialRulesTransformer(name, literals, rules)
    return transformer


def build_special_match(name, *pattern_and_lambdas):
    """
    Build a SpecialRulesTransformer with no literals (same shape as
    macro-match).
    """

    return build_special_syntax(name, (), *pattern_and_lambdas)


# The end.
