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

Syntax-domain API: Syntax boxing, syntax-rules / macro-rules pattern
compilation, transformer models, and runtime tagged-tuple builders.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from dataclasses import dataclass
from typing import Any

from .exceptions import CompilerError
from .hygiene import (
    apply_syntax_rules_hygiene,
    collect_binding_symbols,
    expand_template_with_hygiene,
)
from .lib import car, cdr, cons, demand_call, gensym, nil, symbol
from .matcher import (BIND, LIST, LITERAL, LITERAL_SYM, MATCH, REPEAT, REST,
                      VALUE, Matcher, RepeatResult, match)
from .matcher import from_tuple as matcher_from_tuple
from .types import (build_proper, get_position, is_nil, is_pair,
                    is_proper, is_symbol, unpack)


__all__ = (
    'Syntax',
    'box_match_capture',
    'parse_rule_pattern',
    'syntax_pattern_to_matcher',
    'collect_pattern_variable_symbols',
    'SyntaxRule',
    'SyntaxRulesSegment',
    'SyntaxRulesTransformer',
    'build_syntax',
    'build_syntax_rules_transformer',
    'MacroRule',
    'MacroRulesSegment',
    'MacroRulesTransformer',
    'build_macro_syntax',
    'parse_macro_rules_rows',
    'unbox_macro_with_hygiene',
)


_symbol_match = symbol("match")
_symbol_rule = symbol("rule")


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
                "cannot iterate Syntax with non-pair datum")

        cell = d
        for elem in unpack(d):
            if is_pair(cell) and not is_nil(cell):
                pos = get_position(cell) or self.position
                yield Syntax(elem, pos, self.tag)
                cell = cdr(cell)
            else:
                yield Syntax(elem, self.position, self.tag)


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


def parse_explicit_rule_pattern(inner, literals, source_ctx):
    """
    Placeholder for the **MATCH** pattern language (rich explicit-rule inner).

    This language is **Koskari surface syntax** for the same expressive matcher
    IR that specials build with the ``MATCH`` combinator in ``koskari.matcher``.
    It is **not** a parallel ad-hoc semantics: the aim is **parity** with
    combinator-built matchers. Distinct from **RULE** patterns
    (``syntax_pattern_to_matcher``). Used for ``(syntax-match …)`` /
    ``(macro-match …)`` rows shaped ``((rule INNER) …)`` and for ``(rule INNER)``
    escapes inside **RULE** patterns once implemented.

    :param inner: Single form inside ``(rule …)``
    :param literals: Literal symbols for this segment
    :param source_ctx: Original rule form for error reporting
    :raises CompilerError: until MATCH parsing is implemented
    """

    raise CompilerError("rule pattern mode not implemented yet", source_ctx)


def parse_rule_pattern(pattern_expr, literals, source_ctx):
    """
    Dispatch **RULE** pattern source (``syntax-rules`` / ``macro-rules``).

    If the pattern is ``(match INNER)``, lower ``INNER`` with
    ``syntax_pattern_to_matcher``. If it is ``(rule INNER)``, delegate to
    ``parse_explicit_rule_pattern`` (**MATCH**; not implemented yet). Otherwise
    treat the whole tree as a Scheme-style pattern via
    ``syntax_pattern_to_matcher``.

    The **macro spec head** (``syntax-rules`` vs ``syntax-match``, etc.)
    selects RULE vs MATCH for the whole ruleset; this function only handles the
    **RULE** pattern column and nested ``match`` / ``rule`` escapes.
    """

    if is_pair(pattern_expr):
        head, rest = pattern_expr
        if head == _symbol_match:
            if not is_proper(rest) or cdr(rest) is not nil:
                raise CompilerError(
                    "match pattern must have exactly one form", pattern_expr)

            return syntax_pattern_to_matcher(car(rest), literals, source_ctx)

        if head == _symbol_rule:
            if not is_proper(rest) or cdr(rest) is not nil:
                raise CompilerError(
                    "rule pattern must have exactly one form", pattern_expr)

            return parse_explicit_rule_pattern(car(rest), literals, source_ctx)

    return syntax_pattern_to_matcher(pattern_expr, literals, source_ctx)


def syntax_pattern_to_matcher(pattern_expr, literals, source_ctx):
    """
    Lower a syntax-rules pattern cons tree to matcher objects.
    """

    if pattern_expr is Ellipsis:
        raise CompilerError("invalid ellipsis placement in pattern", source_ctx)

    if is_symbol(pattern_expr):
        if pattern_expr in literals:
            return MATCH(LITERAL(pattern_expr))
        return MATCH(REST(pattern_expr))

    if not is_pair(pattern_expr):
        return MATCH(LITERAL(pattern_expr))

    return MATCH(*_syntax_pattern_specs(
        list(unpack(pattern_expr)), literals, source_ctx))


def _syntax_elem_matcher(pat, literals, source_ctx):
    """
    Lower a single syntax-rules pattern element to matcher objects

    Symbols become LITERAL or VALUE; non-pairs become LITERAL; proper lists
    become LIST of nested specs from the same pattern language

    :param pat: pattern datum (symbol, pair, or literal)
    :param literals: literal symbols for this segment
    :param source_ctx: form for error reporting
    :raises CompilerError: on invalid ellipsis placement
    :return: a matcher spec (LITERAL, VALUE, or LIST wrapping inner specs)
    """

    if pat is Ellipsis:
        raise CompilerError("invalid ellipsis placement in pattern", source_ctx)

    if is_symbol(pat):
        if pat in literals:
            return LITERAL(pat)
        return VALUE(pat)

    if not is_pair(pat):
        return LITERAL(pat)

    return LIST(*_syntax_pattern_specs(
        list(unpack(pat)), literals, source_ctx))


def _syntax_pattern_specs(p_items, literals, source_ctx):
    """
    Build matcher specs from a flat sequence of pattern list elements

    Treats ``(subpat ...)`` pairs with ``...`` as zero-or-more REPEAT regions;
    other elements are passed to :func:`_syntax_elem_matcher`

    :param p_items: list of elements from ``list(unpack(...))`` of a pattern list
    :param literals: literal symbols for this segment
    :param source_ctx: form for error reporting
    :return: list of matcher specs (LITERAL, VALUE, LIST, REPEAT, etc.)
    """

    specs = []
    i = 0
    while i < len(p_items):
        if i + 1 < len(p_items) and p_items[i + 1] is Ellipsis:
            pat = p_items[i]
            inner = _syntax_elem_matcher(pat, literals, source_ctx)
            slot = gensym('_repeat')
            specs.append(REPEAT(slot, inner, min_count=0))
            i += 2
            continue

        specs.append(_syntax_elem_matcher(p_items[i], literals, source_ctx))
        i += 1

    return specs


def collect_pattern_variable_symbols(pattern_expr, literals):
    """
    Pattern variables in pre-order, excluding literals and ellipsis markers.
    Each symbol appears once (first occurrence). Used for macro-rules formals.
    """

    seen = []

    def walk(expr):
        if expr is Ellipsis:
            return

        if is_symbol(expr):
            if expr in literals:
                return

            if expr not in seen:
                seen.append(expr)

            return

        if not is_pair(expr):
            return

        items = list(unpack(expr))
        i = 0
        while i < len(items):
            if i + 1 < len(items) and items[i + 1] is Ellipsis:
                walk(items[i])
                i += 2
            else:
                walk(items[i])
                i += 1

    walk(pattern_expr)
    return seen


_tag_syntax_rules = symbol("syntax/rules")
_tag_list = symbol("syntax/list")
_tag_cons = symbol("syntax/cons")

_SYNTAX_RULES_HEAD_PATTERN = MATCH(
    LITERAL_SYM("syntax-rules"),
    MATCH(BIND("literals"), REST("rules")))
_SYNTAX_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))

_MACRO_RULES_HEAD_PATTERN = MATCH(
    LITERAL_SYM("macro-rules"),
    MATCH(BIND("literals"), REST("rules")))
_MACRO_RULE_PATTERN = MATCH(REPEAT("rules", BIND("rule")))


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


def _maybe_unwrap_repeat_macro_kwarg(val, formal_sym):
    """
    Normalize repeat-shaped macro kwargs to bare values when unambiguous

    If ``val`` is a list of one-key dicts each holding ``formal_sym``, return
    the list of those values; otherwise return ``val`` unchanged

    :param val: keyword value from macro match (often list of per-repeat dicts)
    :param formal_sym: formal name for this argument
    :return: unwrapped list or original ``val``
    """

    if not isinstance(val, list) or not val:
        return val

    for row in val:
        if not isinstance(row, dict) or len(row) != 1 or formal_sym not in row:
            return val

    return [row[formal_sym] for row in val]


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

        raise CompilerError("no matching define-syntax rule", source_obj)


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
        xi = id(x)
        if xi in seen:
            return x
        seen.add(xi)

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
    formals: tuple
    lambda_form: Any = None
    procedure: Any = None


    def expand(self, source_obj, literals):
        raw = match(source_obj, self.pattern_matcher, literals=literals)
        if raw is None:
            return None

        pos_fb = get_position(source_obj) if is_pair(source_obj) else None
        kwargs = _match_dict_to_syntax_kwargs(raw, position_fallback=pos_fb)
        if kwargs is None:
            return None

        for s in self.formals:
            if s not in kwargs:
                return None

        if self.procedure is None:
            raise CompilerError("internal: macro rule missing procedure")

        args = [_maybe_unwrap_repeat_macro_kwarg(kwargs[s], s) for s in self.formals]
        return demand_call(self.procedure, *args)


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

        raise CompilerError("no matching macro-rules rule", source_obj)


def build_syntax(name, tagged_tuple):
    transformer = SyntaxRulesTransformer.from_tuple(tagged_tuple)
    if name is not None:
        transformer.name = name
    return transformer


def build_macro_syntax(name, literals_tuple, *pattern_and_lambdas):
    """
    Build a MacroRulesTransformer from compile-time lowered arguments:
    name, serialized literals tuple, then alternating (pattern_tuple, fn) pairs.
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise CompilerError("build-macro-rules requires pattern/lambda pairs")

    literals = frozenset(_deserialize_form(x) for x in literals_tuple)
    rules = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pattern_tup = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        pm = matcher_from_tuple(pattern_tup[0])
        formals = tuple(_deserialize_form(x) for x in pattern_tup[1])
        rules.append(MacroRule(pm, formals, procedure=fn))

    transformer = MacroRulesTransformer(name, literals, rules)
    return transformer


def build_syntax_rules_transformer(name, spec):
    head_m = match(spec, _SYNTAX_RULES_HEAD_PATTERN)
    if head_m is None:
        raise CompilerError("define-syntax requires a syntax-rules form", spec)

    literals_expr = head_m["literals"].source
    rules_expr = head_m["rules"].source

    literals = frozenset(unpack(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _SYNTAX_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise CompilerError("syntax-rules requires at least one rule", spec)

    rules = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise CompilerError("invalid syntax-rules rule", raw_rule)

        pattern_src, template_tail = raw_rule
        if not is_proper(template_tail) or cdr(template_tail) is not nil:
            raise CompilerError(
                "syntax-rules rule must have pattern and template",
                raw_rule)
        template_expr = car(template_tail)
        pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
        rules.append(SyntaxRule(pattern_matcher, template_expr))

    return SyntaxRulesTransformer(name, literals, rules)


def parse_macro_rules_rows(name, spec):
    """
    Parse a (macro-rules LITERALS ... RULES) spec into matchers and body datums

    No compile/lambda or build-macro-rules source is emitted here; compile
    specials lower that from the returned rows.

    :param name: macro name symbol (included in literals)
    :param spec: pair tree matching the macro-rules head shape
    :return: ``(literals, rows)`` where each row is ``(pattern_matcher,
        formals_tuple, list of body forms)``
    :raises CompilerError: when the spec is not a valid macro-rules form
    """

    head_m = match(spec, _MACRO_RULES_HEAD_PATTERN)
    if head_m is None:
        raise CompilerError("define-syntax requires macro-rules form", spec)

    literals_expr = head_m["literals"].source
    rules_expr = head_m["rules"].source

    literals = frozenset(unpack(literals_expr))
    literals = literals | {name}

    rules_m = match(rules_expr, _MACRO_RULE_PATTERN)
    if rules_m is None or not rules_m["rules"].matches:
        raise CompilerError("macro-rules requires at least one rule", spec)

    rows = []
    for res, _sp in rules_m["rules"].matches:
        raw_rule = res["rule"].source
        if not is_pair(raw_rule):
            raise CompilerError("invalid macro-rules rule", raw_rule)

        if not is_proper(raw_rule):
            raise CompilerError("macro-rules rule must be a proper list", raw_rule)

        items = list(unpack(raw_rule))
        if len(items) < 2:
            raise CompilerError(
                "macro-rules rule needs pattern and at least one body form",
                raw_rule)

        pattern_src = items[0]
        body_forms = items[1:]
        if not body_forms:
            raise CompilerError("macro-rules rule requires a body", raw_rule)

        pattern_matcher = parse_rule_pattern(pattern_src, literals, raw_rule)
        vars_ordered = collect_pattern_variable_symbols(pattern_src, literals)
        rows.append((pattern_matcher, tuple(vars_ordered), body_forms))

    return literals, tuple(rows)


# The end.
