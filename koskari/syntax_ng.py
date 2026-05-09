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
koskari.syntax_ng

Next-generation syntax dispatch scaffolding: ``Syntax`` (a ``Compiled``
dispatcher), transform arms, ``Datum`` boxing for hygiene guards, and a
syntax-rules-compatible pattern lowerer to ``matcher_ng.Pattern``.

The compiled dispatcher type here is ``syntax_ng.Syntax``. The legacy
source wrapper remains ``koskari.syntax.Syntax``. This module does not
implement MATCH-dialect ``parse_explicit_rule_pattern`` (``matcher/foo``
registry); only RULE-style syntax-rules patterns.

Bare-pattern ellipsis ``(id ...)`` uses an inner capture slot ``id__sr``
per iteration so freeze-time slot names stay distinct from the outer
repeat list slot. Callers can flatten frames to legacy ``RepeatResult``
shape later. An empty ellipsis region matches a subject with zero spine
elements (``()``), not ``(nil,)``.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from dataclasses import dataclass
from typing import Any, Protocol, runtime_checkable

from koskari.compile.compiler import Compiled
from koskari.exceptions import CompilerError, SpecialError
from koskari.hygiene_ng import (
    apply_syntax_rules_hygiene,
    collect_binding_symbols,
    expand_template_with_hygiene,
)
from koskari.lib import Matcher, cdr, gensym, nil, symbol
from koskari.matcher_ng import (
    ABSENT,
    CAPTURE,
    FORM,
    GROUP,
    LITERAL,
    REST,
    SYMBOL,
    HygienicMatchCompiler,
    check_pattern_arg,
    repeat_capture,
)
from koskari.types import (
    get_position,
    is_nil,
    is_pair,
    is_proper,
    is_symbol,
    unpack,
)


__all__ = (
    "Datum",
    "MacroTransform",
    "SpecialTransform",
    "Syntax",
    "SyntaxTransform",
    "Transform",
    "parse_syntax_rules_pattern",
    "special",
)


_VALID_UNPACK_MODES = frozenset((None, "match", "kwargs"))


_symbol_match = symbol("match")


def _validate_unpack_mode(unpack):
    """
    Raise ``ValueError`` when unpack is not a supported special-form unpack mode
    """

    if unpack not in _VALID_UNPACK_MODES:
        raise ValueError(
            "unpack must be None, 'match', or 'kwargs', got %r" % (unpack,))


def _init_pattern_fields(instance, pattern, handler):
    """
    Attach pattern, frozen matcher tuple, hygiene plan, and thawed matcher
    """

    check_pattern_arg(pattern, "Transform pattern")
    if handler is not None and not callable(handler):
        raise TypeError(
            "Transform handler must be callable or None, got %r"
            % (type(handler).__name__,))
    instance.pattern = pattern
    instance.handler = handler
    hc = HygienicMatchCompiler()
    instance.matcher_tuple = hc.lower(pattern)
    instance.hygiene = hc.build_hygiene()
    instance._matcher = Matcher.from_tuple(instance.matcher_tuple)


def _tail_subject(source_obj):
    """
    Match subject for syntax-rules-style patterns: spine of ``cdr(source_obj)``
    """

    if not is_pair(source_obj):
        return ()
    rest = cdr(source_obj)
    if rest is nil:
        return ()
    if not is_proper(rest):
        raise CompilerError(
            "syntax pattern match requires a proper tail", source_obj)
    return tuple(unpack(rest))


def _filter_captures(captures):
    """
    Drop ``ABSENT`` entries from native ``MatchResult.captures``
    """

    return {
        str(k): v
        for k, v in captures.items()
        if v is not ABSENT
    }


def _captures_to_bindings(captures):
    """
    Map string capture keys to koskari symbols for ``apply_syntax_rules_hygiene``
    """

    out = {}
    for k, v in _filter_captures(captures).items():
        out[symbol(k)] = v
    return out


def _make_special_invoke(user_fn, unpack):
    """
    Build one of three invoke closures (no per-call unpack branch)
    """

    if unpack is None:

        def invoke_none(
                compiler,
                source_obj,
                tc,
                match_result):
            return user_fn(compiler, source_obj, tc)

        return invoke_none

    if unpack == "match":

        def invoke_match(
                compiler,
                source_obj,
                tc,
                match_result):
            caps = _filter_captures(match_result.captures)
            return user_fn(compiler, caps, tc)

        return invoke_match

    if unpack == "kwargs":

        def invoke_kwargs(
                compiler,
                source_obj,
                tc,
                match_result):
            caps = _filter_captures(match_result.captures)
            return user_fn(compiler, tc=tc, **caps)

        return invoke_kwargs

    raise ValueError("internal: bad unpack %r" % (unpack,))


def _unbox_datum_in_macro_output(expanded):
    """
    Strip ``Datum`` wrappers from procedural macro output (mirrors legacy
    ``unbox_macro`` for ``koskari.syntax.Syntax``)
    """

    seen = set()

    def walk(x):
        if isinstance(x, Datum):
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
                if isinstance(head, Datum):
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


@runtime_checkable
class Transform(Protocol):
    """
    One clause of a ``Syntax`` dispatcher: frozen ``matcher_ng`` pattern,
    hygiene plan, and matcher-backed ``try_expand``
    """

    pattern: Any
    matcher_tuple: tuple
    hygiene: Any
    handler: Any

    def try_expand(self, compiler, source_obj, tc):
        """
        Try this arm on ``source_obj``; return compile result or ``None``

        :param compiler: active compiler instance
        :param source_obj: full special form pair ``(head . tail)``
        :param tc: tail-call flag
        :return: fragment or form for ``compile_special``, or ``None`` when the
            pattern does not match
        """

        ...


@dataclass(frozen=True)
class Datum(object):
    """
    Wraps a Koskari datum with optional source position and optional tag.
    Same shape as ``koskari.syntax.Syntax``; used as an opaque box so hygiene
    walks skip internals until a syntax or macro transform unboxes

    :param datum: underlying form (pair, symbol, nil, literals)
    :param position: source position tuple or None when unknown
    :param tag: optional interpretation hint; reserved for future use
    """

    datum: Any
    position: Any = None
    tag: Any = None


    def __iter__(self):
        """
        Yield ``Datum`` values for each element from the pair spine

        :raises TypeError: if ``datum`` is not a pair
        :return: iterator of Datum instances
        """

        d = self.datum

        if not is_pair(d):
            raise TypeError(
                "cannot iterate Datum with non-pair datum: %r" % (d,))

        cell = d
        for elem in unpack(d):
            if is_pair(cell) and not is_nil(cell):
                pos = get_position(cell) or self.position
                yield Datum(elem, pos, self.tag)
                cell = cdr(cell)
            else:
                yield Datum(elem, self.position, self.tag)


    @property
    def car(self):
        return self.datum.car


    @property
    def cdr(self):
        return self.datum.cdr


class SyntaxTransform(object):
    """
    Template arm: match tail, then ``apply_syntax_rules_hygiene`` on template
    """

    def __init__(self, pattern, template=nil):
        """
        :param pattern: matcher_ng pattern tree
        :param template: syntax-rules template form (often contains pattern vars)
        """

        _init_pattern_fields(self, pattern, None)
        self.template = template


    def try_expand(self, compiler, source_obj, tc):
        """
        Match ``cdr(source_obj)``; on success expand ``template`` with captures

        :param compiler: active compiler
        :param source_obj: full special form pair
        :param tc: tail-call flag (unused for template expansion)
        :return: expanded form or ``None`` when the pattern does not match
        """

        subject = _tail_subject(source_obj)
        result = self._matcher.match(subject)
        if not result.matched:
            return None
        bindings = _captures_to_bindings(result.captures)
        return apply_syntax_rules_hygiene(
            self.template, bindings, compiler.find_compiled)


class MacroTransform(object):
    """
    Procedural arm: invoke handler then ``hygiene_ng`` on expanded output
    """

    def __init__(self, pattern, handler, unpack=None):
        """
        :param pattern: matcher_ng pattern tree
        :param handler: Python callable (arity depends on ``unpack``)
        :param unpack: ``None``, ``match``, or ``kwargs`` (fixed at construction)
        """

        _validate_unpack_mode(unpack)
        if handler is None or not callable(handler):
            raise TypeError(
                "MacroTransform handler must be callable, got %r"
                % (type(handler).__name__,))
        _init_pattern_fields(self, pattern, handler)
        self._unpack = unpack
        self._invoke = _make_special_invoke(handler, unpack)


    def try_expand(self, compiler, source_obj, tc):
        """
        Match tail, run handler, then procedural macro hygiene on the result

        :param compiler: active compiler
        :param source_obj: full special form pair
        :param tc: tail-call flag passed through when ``unpack`` is ``kwargs``
        :return: hygiened expanded form or ``None`` when the pattern does not
            match
        """

        subject = _tail_subject(source_obj)
        result = self._matcher.match(subject)
        if not result.matched:
            return None
        expanded = self._invoke(compiler, source_obj, tc, result)
        syms = collect_binding_symbols(expanded, compiler.find_compiled)
        hygiened = expand_template_with_hygiene(expanded, {}, syms, {})
        return _unbox_datum_in_macro_output(hygiened)


class SpecialTransform(object):
    """
    Special arm: handler return is the compile result (no automatic hygiene)
    """

    def __init__(self, pattern, handler, unpack=None):
        """
        :param pattern: matcher_ng pattern tree
        :param handler: Python callable (arity depends on ``unpack``)
        :param unpack: ``None``, ``match``, or ``kwargs`` (fixed at construction)
        """

        _validate_unpack_mode(unpack)
        if handler is None or not callable(handler):
            raise TypeError(
                "SpecialTransform handler must be callable, got %r"
                % (type(handler).__name__,))
        _init_pattern_fields(self, pattern, handler)
        self._unpack = unpack
        self._invoke = _make_special_invoke(handler, unpack)


    def try_expand(self, compiler, source_obj, tc):
        """
        Match tail; on success return handler result unchanged

        :param compiler: active compiler
        :param source_obj: full special form pair
        :param tc: tail-call flag passed through when ``unpack`` is ``kwargs``
        :return: compile product or ``None`` when the pattern does not match
        """

        subject = _tail_subject(source_obj)
        result = self._matcher.match(subject)
        if not result.matched:
            return None
        return self._invoke(compiler, source_obj, tc, result)


class Syntax(Compiled):
    """
    Ordered dispatcher over ``Transform`` clauses (compiled form). Try order
    follows ``transforms`` tuple order; use ``__add__`` to prepend clauses

    :param name: primary compile-time symbol for this form
    :param aliases: additional symbols resolving to this dispatcher
    :param transforms: ordered transforms tried on each expansion
    """

    def __init__(self, name, aliases=(), transforms=()):
        """
        :param name: symbol or str naming this syntax object
        :param aliases: iterable of extra names
        :param transforms: iterable of ``Transform`` instances
        """

        self.name = name
        self.aliases = tuple(aliases)
        self.transforms = tuple(transforms)
        for t in self.transforms:
            if not isinstance(t, Transform):
                raise TypeError(
                    "Syntax transforms must be Transform instances, got %r"
                    % (type(t).__name__,))


    def compile_impl(self, compiler, source_obj, tc):
        """
        Try each transform in order; first non-``None`` result wins

        :param compiler: active compiler instance
        :param source_obj: full special form being compiled
        :param tc: tail-call flag
        :raises SpecialError: when no arm matches
        :return: compile product from the winning transform
        """

        for t in self.transforms:
            out = t.try_expand(compiler, source_obj, tc)
            if out is not None:
                return out
        raise SpecialError("no matching syntax rule", source_obj)


    def __add__(self, other):
        """
        Return a new ``Syntax`` with ``other``'s transforms prepended before
        ``self``'s (``other`` is tried first at expansion time)

        :param other: ``Transform`` or ``Syntax``
        :rtype: Syntax
        """

        if isinstance(other, Transform):
            new_transforms = (other,) + self.transforms
        elif isinstance(other, Syntax):
            new_transforms = other.transforms + self.transforms
        else:
            return NotImplemented
        return Syntax(self.name, self.aliases, new_transforms)


def _normalize_special_patterns(patterns):
    """
    Accept one ``Pattern`` or an iterable of patterns for ordered disjunction
    """

    if isinstance(patterns, (list, tuple)):
        out = tuple(patterns)
    else:
        out = (patterns,)
    for i, p in enumerate(out):
        check_pattern_arg(p, "special patterns[%s]" % i)
    return out


def special(form_symbol, *, patterns, unpack=None):
    """
    Build a ``Syntax`` dispatcher whose arms are ``SpecialTransform`` instances
    wrapping the decorated function. ``unpack`` selects one of three fixed
    invoke shapes at decoration time (no per-call unpack dispatch)

    :param form_symbol: primary compile-time head symbol for this form
    :param patterns: one ``matcher_ng.Pattern`` or an iterable (try-order)
    :param unpack: ``None`` (receive ``compiler, source_obj, tc``),
        ``match`` (receive ``compiler, captures_dict, tc``), or ``kwargs``
        (captures merged after ``tc``)
    """

    _validate_unpack_mode(unpack)

    def deco(fn):
        """
        :param fn: Python handler implementing this special form
        :rtype: Syntax
        """

        if not callable(fn):
            raise TypeError(
                "special decorator requires a callable, got %r"
                % (type(fn).__name__,))
        plist = _normalize_special_patterns(patterns)
        transforms = tuple(SpecialTransform(p, fn, unpack=unpack) for p in plist)
        return Syntax(form_symbol, (), transforms)

    return deco


def parse_syntax_rules_pattern(pattern_expr, literals, source_ctx):
    """
    Lower a syntax-rules RULE pattern (cons tree) to a ``matcher_ng.Pattern``.

    Supports the same surface as ``parse_rule_pattern`` /
    ``syntax_pattern_to_matcher`` in ``compile/specials/syntax.py`` (RULE
    dialect only). Does not implement MATCH-dialect ``matcher/foo`` forms.

    :param pattern_expr: koskari pattern datum
    :param literals: set or frozenset of literal symbols
    :param source_ctx: value recorded in error messages
    :rtype: Pattern
    """

    if not isinstance(literals, (set, frozenset)):
        raise TypeError(
            "literals must be a set or frozenset, got %r"
            % (type(literals).__name__,))

    return _parse_rule_pattern(pattern_expr, literals, source_ctx)


def _parse_rule_pattern(pattern_expr, literals, source_ctx):
    if is_pair(pattern_expr):
        head, rest = pattern_expr
        if head == _symbol_match:
            if (not is_proper(rest)
                    or rest.cdr is not nil
                    or rest is nil):
                raise ValueError(
                    "match pattern must have exactly one form: %r (context %r)"
                    % (pattern_expr, source_ctx))
            return _syntax_pattern_to_pattern(rest.car, literals, source_ctx)

    return _syntax_pattern_to_pattern(pattern_expr, literals, source_ctx)


def _syntax_pattern_to_pattern(pattern_expr, literals, source_ctx):
    if pattern_expr is Ellipsis:
        raise TypeError(
            "invalid ellipsis placement in pattern: %r (context %r)"
            % (pattern_expr, source_ctx))

    if is_symbol(pattern_expr):
        if pattern_expr in literals:
            return GROUP(LITERAL(pattern_expr))
        return GROUP(REST(str(pattern_expr)))

    if not is_pair(pattern_expr):
        return GROUP(LITERAL(pattern_expr))

    return GROUP(*_syntax_pattern_specs(
        list(unpack(pattern_expr)), literals, source_ctx))


def _syntax_elem_pattern(pat, literals, source_ctx):
    if pat is Ellipsis:
        raise ValueError(
            "invalid ellipsis placement in pattern: %r (context %r)"
            % (pat, source_ctx))

    if is_symbol(pat):
        if pat in literals:
            return LITERAL(pat)
        return SYMBOL(str(pat))

    if not is_pair(pat):
        return LITERAL(pat)

    return FORM(GROUP(*_syntax_pattern_specs(
        list(unpack(pat)), literals, source_ctx)))


def _syntax_pattern_specs(p_items, literals, source_ctx):
    specs = []
    i = 0
    while i < len(p_items):
        if i + 1 < len(p_items) and p_items[i + 1] is Ellipsis:
            pat = p_items[i]
            if is_symbol(pat) and pat not in literals:
                slot = str(pat)
                inner = CAPTURE(slot + "__sr")
            else:
                slot = str(gensym("_repeat"))
                inner = _syntax_elem_pattern(pat, literals, source_ctx)
            specs.append(repeat_capture(slot, inner))
            i += 2
            continue

        specs.append(_syntax_elem_pattern(p_items[i], literals, source_ctx))
        i += 1

    return specs


# The end.
