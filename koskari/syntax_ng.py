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


from abc import ABC
from dataclasses import dataclass
from typing import Any

from koskari.compile.compiler import Compiled
from koskari.lib import cdr, gensym, nil, symbol
from koskari.matcher_ng import (CAPTURE, FORM, GROUP, LITERAL, REST, SYMBOL,
                                HygienicMatchCompiler, check_pattern_arg,
                                repeat_capture)
from koskari.types import (get_position, is_nil, is_pair, is_proper, is_symbol,
                           unpack)


__all__ = (
    "Datum",
    "MacroTransform",
    "SpecialTransform",
    "Syntax",
    "SyntaxTransform",
    "Transform",
    "parse_syntax_rules_pattern",
)


_symbol_match = symbol("match")


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


class Transform(ABC):
    """
    One clause of a ``Syntax`` dispatcher: frozen ``matcher_ng`` pattern,
    hygiene plan, and optional handler callable (filled in later)

    :param pattern: lowered syntax-rules pattern
    :param handler: optional Python callable invoked after a match
    """

    def __init__(self, pattern, handler=None):
        """
        :param pattern: matcher_ng pattern tree
        :param handler: optional callable; subclasses document arity
        """

        check_pattern_arg(pattern, "Transform pattern")
        if handler is not None and not callable(handler):
            raise TypeError(
                "Transform handler must be callable or None, got %r"
                % (type(handler).__name__,))
        self.pattern = pattern
        self.handler = handler
        hc = HygienicMatchCompiler()
        self.matcher_tuple = hc.lower(pattern)
        self.hygiene = hc.build_hygiene()



class SyntaxTransform(Transform):
    """
    Template-only arm: future expansion will run ``hygiene_ng`` on a template
    """



class MacroTransform(Transform):
    """
    Procedural arm: future expansion will invoke the handler then a hygiene pass
    """



class SpecialTransform(Transform):
    """
    Special arm: handler return is the compile result without automatic hygiene
    """



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
        Compile-time entry point (stub)

        :raises NotImplementedError: always until dispatch is implemented
        """

        raise NotImplementedError("Syntax.compile_impl is not implemented yet")



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
