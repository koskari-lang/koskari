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
koskari.hygiene_ng

Hygiene discovery and template expansion driven by explicit matcher-time
Hygiene plans plus stable Special-object hooks
(match_for_hygiene, hygiene), instead of legacy matcher-spec introspection.

Each binder or scope-region slot maps to the Pattern or Composite instance
that produced it; hygiene interpretation lives on those objects.
Portable plans use ``Hygiene.to_tuple`` / ``Hygiene.from_tuple`` with tag
``HYGIENE`` (see method docstrings).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from dataclasses import dataclass
from typing import Mapping

from koskari.exceptions import CompilerError, SpecialError
from koskari.lib import gensym, nil, keyword
from koskari.types import build_proper, is_pair, is_symbol, unpack


__all__ = (
    "Hygiene",
    "introduced_symbols_from_match",
    "scope_region_targets",
    "binder_recurse_targets",
    "collect_binding_symbols",
    "expand_template_with_hygiene",
    "apply_syntax_rules_hygiene",
)


_HYGIENE_TAG = keyword("HYGIENE")


@dataclass(frozen=True)
class Hygiene(object):
    """
    Freeze-time hygiene registration parallel to matcher composites.

    :param binders: binder slot names mapped to owning Pattern instances
    :param scope_regions: scope-region slot names mapped to owning Pattern
        instances
    """

    binders: Mapping[str, object]
    scope_regions: Mapping[str, object]


    def is_empty(self):
        """
        True when no binder or scope-region registrations exist
        """

        return not bool(self.binders or self.scope_regions)


    def is_binder(self, slot):
        """
        True when slot carries binder semantics for this plan
        """

        return slot in self.binders


    def is_scope_region(self, slot):
        """
        True when slot is registered as a scope-region carrier
        """

        return slot in self.scope_regions


    def __bool__(self):
        return bool(self.binders or self.scope_regions)


    def to_tuple(self):
        """
        Serialize to ``("HYGIENE", binders_rows, scope_rows)`` using each
        owner's ``hygiene_wire()``

        :rtype: tuple
        """

        binders_rows = tuple(
            owner.hygiene_wire() for owner in self.binders.values())
        scope_rows = tuple(
            owner.hygiene_wire() for owner in self.scope_regions.values())
        return (_HYGIENE_TAG, binders_rows, scope_rows)


    @classmethod
    def from_tuple(cls, t):
        """
        Rebuild from ``to_tuple`` output

        :param t: ``("HYGIENE", binders_rows, scope_rows)``
        :rtype: Hygiene
        """

        if not (isinstance(t, tuple) and len(t) == 3 and t[0] == _HYGIENE_TAG):
            raise ValueError(
                "hygiene tuple must be (%r, binders, scopes), got %r"
                % (_HYGIENE_TAG, t))
        _, binders_entries, scope_entries = t

        binders = {}
        scope_regions = {}
        for row in binders_entries:
            pat = hygiene_from_wire_row(row)
            binders[pat._slot] = pat
        for row in scope_entries:
            pat = hygiene_from_wire_row(row)
            scope_regions[pat._slot] = pat
        return cls(binders, scope_regions)


_MATCHER_REGISTRY = None


def hygiene_from_wire_row(row):
    """
    Rebuild a pattern from a ``hygiene_wire`` tuple using the matcher_ng
    registry

    :param row: tuple whose first element is a registered matcher head
    :rtype: object
    """

    global _MATCHER_REGISTRY
    if _MATCHER_REGISTRY is None:
        from .matcher_ng import MATCHER_REGISTRY as _MATCHER_REGISTRY

    factory = _MATCHER_REGISTRY[row[0]]
    return factory.matcher_ng_wire_class.hygiene_from_wire(row)


def introduced_symbols_from_match(matched, hygiene):
    """
    Collect symbols introduced by binder composites under hygiene rules.

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: compiled hygiene plan or None
    :rtype: set of koskari symbols
    """

    out = set()
    if not (matched and hygiene):
        return out

    for owner in hygiene.binders.values():
        for sym in owner.iter_introduced_symbols(matched):
            if sym is not None and is_symbol(sym):
                out.add(sym)

    return out


def binder_recurse_targets(matched, hygiene):
    """
    Yield forms nested under binder slots that must be walked for nested
    binder discovery (initializer arms).

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: compiled hygiene plan or None
    :rtype: iterable of forms suitable for collect_binding_symbols recursion
    """

    if matched is None or hygiene is None or hygiene.is_empty():
        return

    for owner in hygiene.binders.values():
        for form in owner.iter_recurse_targets(matched):
            if form is not None:
                yield form


def scope_region_targets(matched, hygiene):
    """
    Enumerate scope-region walk roots: each owner yields its capture once.
    ``collect_binding_symbols`` walks koskari pair trees only; tuple values
    from captures are not source and contribute no symbols

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: compiled hygiene plan or None
    """

    if matched is None or hygiene is None or hygiene.is_empty():
        return

    for owner in hygiene.scope_regions.values():
        for form in owner.iter_scope_targets(matched):
            if form is not None:
                yield form


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    if is_pair(expr):
        for elem in unpack(expr):
            if is_symbol(elem):
                out.add(elem)
            elif is_pair(elem):
                _collect_symbols(elem, out)


def _repeat_count(template, bindings):
    syms = set()
    _collect_symbols(template, syms)
    counts = {
        len(bindings[s])
        for s in syms
        if s in bindings and isinstance(bindings[s], list)
    }
    if not counts:
        return 0
    if len(counts) != 1:
        raise CompilerError(
            'incompatible ellipsis repetition lengths', template)
    return next(iter(counts))


def expand_template_with_hygiene(
        template,
        bindings,
        binding_symbols,
        introduced=None,
        index=None):
    """
    Substitute pattern bindings and assign fresh gensyms for introduced
    template identifiers.

    :param template: template form (pairs, symbols, or Syntax-like opaque)
    :param bindings: dict from pattern variable symbol to expanded value
    :param binding_symbols: set of symbols needing gensym when not bound
    :param introduced: optional map mutated from symbol to gensym
    :param index: ellipsis repetition slice index when repeating
    """

    if introduced is None:
        introduced = {}

    if is_symbol(template):
        if template in bindings:
            val = bindings[template]
            if isinstance(val, list):
                if index is None:
                    raise CompilerError(
                        'ellipsis variable outside repeated template',
                        template)
                return val[index]
            return val

        if template is Ellipsis:
            return template

        if template in binding_symbols:
            if template not in introduced:
                introduced[template] = gensym(str(template))
            return introduced[template]

        return template

    if not is_pair(template):
        return template

    items = list(unpack(template))
    out = []
    i = 0
    while i < len(items):
        elem = items[i]

        rep = (i + 1 < len(items) and items[i + 1] is Ellipsis)
        if not rep:
            out.append(
                expand_template_with_hygiene(
                    elem, bindings, binding_symbols, introduced,
                    index=index))
            i += 1
            continue

        count = _repeat_count(elem, bindings)
        for rep_i in range(count):
            out.append(
                expand_template_with_hygiene(
                    elem, bindings, binding_symbols, introduced,
                    index=rep_i))
        i += 2

    return build_proper(*out)


def collect_binding_symbols(node, find_compiled):
    """
    Walk one template or capture node for binding-symbol discovery.

    Only koskari pair trees participate; tuples are runtime/capture values,
    not source structure, and are never descended. Other atoms yield nothing.

    :param node: koskari pair, nil, tuple (ignored), or other leaf
    :param find_compiled: callable mapping head symbols to compiled specials
    :rtype: set of symbols
    """

    out = set()
    if node is nil or not is_pair(node):
        return out

    head, rest = node
    if not is_symbol(head):
        if rest is nil and is_pair(head):
            return collect_binding_symbols(head, find_compiled)
        for elem in rest.unpack():
            out |= collect_binding_symbols(elem, find_compiled)
        return out

    special = find_compiled(head)
    hygiene = getattr(special, "hygiene", None)
    if special is None or hygiene is None:
        for elem in rest.unpack():
            out |= collect_binding_symbols(elem, find_compiled)
        return out

    matched = None
    try:
        matched = special.match_for_hygiene(rest)
    except SpecialError:
        matched = None

    if matched is None:
        for elem in rest.unpack():
            out |= collect_binding_symbols(elem, find_compiled)
        return out

    out |= introduced_symbols_from_match(matched, hygiene)

    for form in scope_region_targets(matched, hygiene):
        out |= collect_binding_symbols(form, find_compiled)

    for form in binder_recurse_targets(matched, hygiene):
        out |= collect_binding_symbols(form, find_compiled)

    return out


def apply_syntax_rules_hygiene(template, bindings, find_compiled):
    """
    Run binding-symbol discovery and template expansion with hygiene in one
    step.
    """

    syms = collect_binding_symbols(template, find_compiled)
    return expand_template_with_hygiene(template, bindings, syms, {})


# The end.
