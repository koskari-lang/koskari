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
koskari.hygiene

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
from koskari.lib import Datum, cons, gensym, nil
from koskari.types import build_proper, is_nil, is_pair, is_symbol, unpack


__all__ = (
    "Hygiene",
    "introduced_symbols_from_match",
    "scope_region_targets",
    "binder_recurse_targets",
    "collect_binding_symbols",
    "expand_template_with_hygiene",
    "apply_syntax_rules_hygiene",
    "finalize_macro_hygiene_unbox_one_pass",
)


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
            if form is not None and not isinstance(form, Datum):
                yield form


def _hygiene_collect_walk_roots(form):
    """
    Normalize matcher capture values into roots for ``collect_binding_symbols``.

    Scope-region captures (e.g. ``BODY`` / ``REST``) are often ``Datum``-wrapped
    spine slices; peel one layer for the walk only. Tuple captures expand to
    per-element roots. Substitution boxes on the expanded tree are not passed
    through here — only values from ``iter_scope_targets``.
    """

    if form is None:
        return

    if isinstance(form, tuple):
        for elem in form:
            for root in _hygiene_collect_walk_roots(elem):
                yield root
        return

    if isinstance(form, Datum):
        form = form.datum

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
            for root in _hygiene_collect_walk_roots(form):
                yield root


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


def _binding_value_for_expand(val):
    """
    Peel outer ``Datum`` layers so list/pair/tuple shape is visible to template
    expansion. Bindings dict still holds matcher captures unchanged elsewhere
    """

    # if isinstance(orig, Datum):
    #     print('DEBUG hygiene _binding_value_for_expand', 'in=', repr(orig))
    while isinstance(val, Datum):
        val = val.datum
    # if isinstance(orig, Datum):
    #     print('DEBUG hygiene _binding_value_for_expand', 'out=', repr(val))
    return val


def _repeat_count(template, bindings):
    syms = set()
    _collect_symbols(template, syms)
    counts = {
        len(_binding_value_for_expand(bindings[s]))
        for s in syms
        if s in bindings
        and isinstance(
            _binding_value_for_expand(bindings[s]), (list, tuple))
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
    :param bindings: dict from pattern variable symbol to capture value
        (often ``Datum``-wrapped); outer ``Datum`` layers are peeled only
        when expanding list or ellipsis structure, not when returning an
        unexpanded binding reference unchanged
    :param binding_symbols: set of symbols needing gensym when not bound
    :param introduced: optional map mutated from symbol to gensym
    :param index: ellipsis repetition slice index when repeating
    """

    if introduced is None:
        introduced = {}

    # if introduced == {} and index is None:
    #     print(
    #         'DEBUG hygiene expand_template_with_hygiene',
    #         'template=', repr(template),
    #         'binding_keys=', repr(list(bindings.keys()) if bindings else []),
    #     )

    if is_symbol(template):
        if template in bindings:
            val = bindings[template]
            peeled = _binding_value_for_expand(val)
            if isinstance(peeled, (list, tuple)):
                if index is None:
                    raise CompilerError(
                        'ellipsis variable outside repeated template',
                        template)
                return peeled[index]
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

    # TODO: Review. We're losing positional provenance from the template
    # during this pass.
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

        spliced = None
        if is_symbol(elem) and elem in bindings:
            spliced = _binding_value_for_expand(bindings[elem])
        if spliced is not None and not isinstance(spliced, list):
            # Ellipsis splicing: when splicing a captured list/pair spine,
            # each captured element must be inserted as syntax data.
            #
            # Re-expanding spliced elements with the outer bindings causes
            # unintended capture substitution inside inserted syntax (e.g.
            # nested syntax-rules patterns where `name` should remain a
            # pattern variable rather than the outer macro name).
            out.extend(unpack(spliced))
        else:
            count = _repeat_count(elem, bindings)
            for rep_i in range(count):
                out.append(
                    expand_template_with_hygiene(
                        elem, bindings, binding_symbols, introduced,
                        index=rep_i))
        i += 2

    return build_proper(*out)


def finalize_macro_hygiene_unbox_one_pass(form, binding_symbols, find_compiled):
    """
    One pass combining introduced binding-symbol renaming (same ``introduced``
    map semantics as :func:`expand_template_with_hygiene` with empty
    ``bindings``) and ``Datum`` peeling for compilation.

    ``Datum``-wrapped spine cells are peeled in place but not gensymmed; bare
    symbols in ``binding_symbols`` are renamed. That preserves substituted
    pattern captures while freshening template literals

    :param form: macro or syntax-rules output (pairs, atoms, ``Datum``)
    :param binding_symbols: symbols to rename when not bound as pattern vars
    :param find_compiled: unused; reserved for API parity with discovery sites
    :rtype: same root object as ``form`` after in-place updates where applicable
    """

    _ = find_compiled
    introduced = {}
    seen = set()

    def gensym_binding(sym):
        if sym not in introduced:
            introduced[sym] = gensym(str(sym))
        return introduced[sym]

    def peel_car_in_place(nx):
        c = nx.car
        first_pos = None
        while isinstance(c, Datum):
            if first_pos is None:
                first_pos = c.position
            c = c.datum
        if first_pos is not None:
            nx.position = first_pos
        nx.car = c
        return nx.car

    def peel_cdr_datums_in_place(nx):
        tail = nx.cdr
        while isinstance(tail, Datum):
            tail = tail.datum
            nx.cdr = tail
            tail = nx.cdr
        return tail

    def walk_spine(nx):
        while nx is not nil:
            if not is_pair(nx):
                return nx
            i = id(nx)
            if i in seen:
                nx = nx.cdr
                continue
            seen.add(i)

            car_was_datum = isinstance(nx.car, Datum)
            c = peel_car_in_place(nx)

            if is_symbol(c):
                if c in binding_symbols and not car_was_datum:
                    nx.car = gensym_binding(c)
            elif is_pair(c):
                walk_spine(c)

            cdr_was_datum = isinstance(nx.cdr, Datum)
            tail = peel_cdr_datums_in_place(nx)
            if is_nil(tail):
                return nil
            if is_pair(tail):
                nx = tail
                continue
            if is_symbol(tail) and tail in binding_symbols and not cdr_was_datum:
                nx.cdr = gensym_binding(tail)
            return nil
        return nil

    while isinstance(form, Datum):
        form = form.datum
    if is_symbol(form):
        if form in binding_symbols:
            return gensym_binding(form)
        return form
    if is_pair(form):
        walk_spine(form)
    return form


def collect_binding_symbols(node, find_compiled):
    """
    Walk one template or capture node for binding-symbol discovery.

    Only koskari pair trees participate; tuples are runtime/capture values,
    not source structure, and are never descended. Other atoms yield nothing.

    :param node: koskari pair, nil, tuple (ignored), or other leaf
    :param find_compiled: callable mapping head symbols to compiled specials
    :rtype: set of symbols
    """

    #print('DEBUG hygiene collect_binding_symbols', 'node=', repr(node))

    out = set()

    if isinstance(node, Datum):
        return out

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
    if not hygiene:
        # print('DEBUG hygiene collect_binding_symbols', 'no hygiene')
        for elem in rest.unpack():
            out |= collect_binding_symbols(elem, find_compiled)
        return out

    matched = None
    try:
        matched = special.match_for_hygiene(node)
        # print('DEBUG hygiene collect_binding_symbols', 'matched=', repr(matched))
    except SpecialError:
        # print('DEBUG hygiene collect_binding_symbols', 'match error')
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

    if hygiene.is_empty():
        # print('DEBUG hygiene collect_binding_symbols', 'empty hygiene')
        for elem in rest.unpack():
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)

    return out


def apply_syntax_rules_hygiene(template, bindings, find_compiled):
    """
    Run binding-symbol discovery and template expansion with hygiene in one
    step.
    """

    syms = collect_binding_symbols(template, find_compiled)
    return expand_template_with_hygiene(template, bindings, syms, {})


# The end.
