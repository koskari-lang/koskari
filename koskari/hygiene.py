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
koskari.hygiene

Syntax-rules hygiene: discover binding symbols in templates and freshen
introduced identifiers. ``Syntax`` boxes are left in place and never
descended into for either pass.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude via Cursor
"""


from .exceptions import CompilerError, SpecialError
from .lib import cons, gensym, nil
from .matcher import (MatchResult, extract_binder_symbols, is_binder_spec,
                      is_scope_region_spec, match)
from .types import build_proper, is_pair, is_symbol, unpack


__all__ = (
    'collect_binding_symbols',
    'expand_template_with_hygiene',
    'apply_syntax_rules_hygiene',
)


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    if is_pair(expr):
        for elem in unpack(expr):
            _collect_symbols(elem, out)

    # fall-through, don't collect.


def _tail_forms_for_binding_walk(rest):
    if rest is nil or not is_pair(rest):
        return []

    return [e for e in unpack(rest) if e is not None and e is not nil]


def _scope_region_keys(matched, specs):
    keys = set()
    for key, spec in (specs or {}).items():
        if spec is None:
            continue

        scope = getattr(spec, 'scope', None)
        scopes = getattr(spec, 'scopes', None)

        if scope is not None:
            keys.add(scope if isinstance(scope, str) else str(scope))

        if scopes is not None:
            for s in scopes:
                keys.add(s if isinstance(s, str) else str(s))
    if not keys:
        for key, spec in (specs or {}).items():
            if is_scope_region_spec(spec):
                keys.add(key)
    return keys


def collect_binding_symbols(template, find_compiled):
    """
    Walk a template form and collect symbols that participate in syntax-rules
    hygiene (introduced binders per special matcher metadata). ``Syntax`` boxes
    are skipped: they contribute no symbols and are not entered.
    """

    out = set()
    if template is nil or not is_pair(template):
        return out

    head, rest = template
    if not is_symbol(head):
        # A one-form body from REST extract_targets is shaped as ``(FORM . nil)``.
        # The nested special (e.g. ``(compile/lambda ...)``) sits entirely in
        # ``car``; ``cdr`` is nil, so the sibling tail walk below sees nothing
        # and would skip that form unless we descend into ``car`` here.
        # Only recurse ``car`` when it is a pair (nested list/special); a
        # non-pair atom in ``car`` with ``cdr`` nil is not a wrapper.
        if rest is nil and is_pair(head):
            return collect_binding_symbols(head, find_compiled)
        for elem in _tail_forms_for_binding_walk(rest):
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    special = find_compiled(head)
    if special is None or not getattr(special, 'forms', None):
        for elem in _tail_forms_for_binding_walk(rest):
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    try:
        m = match(rest, special.forms, with_specs=True)
    except SpecialError:
        m = None
    if m is None:
        for elem in _tail_forms_for_binding_walk(rest):
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    matched, specs = m
    scope_keys = _scope_region_keys(matched, specs)

    for key, spec in specs.items():
        if is_binder_spec(spec) and key in matched:
            for sym in extract_binder_symbols(spec, matched[key]):
                out.add(sym)

    for key in scope_keys:
        if key in matched:
            val = matched[key]
            if val is None:
                continue
            if isinstance(val, MatchResult):
                for form in val.extract_targets():
                    if form is None:
                        continue
                    out |= collect_binding_symbols(
                        form if is_pair(form) else cons(form, nil),
                        find_compiled)
            else:
                target = val
                if target is None:
                    pass
                elif is_pair(target):
                    for elem in unpack(target):
                        out |= collect_binding_symbols(
                            elem if is_pair(elem) else cons(elem, nil),
                            find_compiled)
                elif isinstance(target, (list, tuple)):
                    for v in target:
                        out |= collect_binding_symbols(
                            v if is_pair(v) else cons(v, nil),
                            find_compiled)
                else:
                    out |= collect_binding_symbols(
                        target if is_pair(target) else cons(target, nil),
                        find_compiled)

    for key, spec in specs.items():
        if is_binder_spec(spec) and key in matched:
            val = matched[key]
            targets = spec.get_recurse_targets(val)
            for v in targets:
                if v is not None:
                    out |= collect_binding_symbols(
                        v if is_pair(v) else cons(v, nil),
                        find_compiled)

    return out


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
    template identifiers. ``Syntax`` values are returned unchanged and never
    entered.

    :param template: template form (pairs, symbols, or ``Syntax``)
    :param bindings: dict from pattern variable to expanded value
    :param binding_symbols: set of symbols needing gensym when not in bindings
    :param introduced: optional map (mutated) from symbol to gensym
    :param index: when repeating, which slice index for ellipsis variables
    :return: expanded form
    """

    if introduced is None:
        introduced = {}

    if is_symbol(template):
        if template in bindings:
            val = bindings[template]
            if isinstance(val, list):
                if index is None:
                    raise CompilerError(
                        'ellipsis variable outside repeated template', template)
                return val[index]
            return val

        if template is Ellipsis:
            return template

        if template in binding_symbols:
            if template not in introduced:
                introduced[template] = gensym(str(template))
            return introduced[template]

        return template

    # This catches our Syntax boxes
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
                    elem, bindings, binding_symbols, introduced, index=index))
            i += 1
            continue

        count = _repeat_count(elem, bindings)
        for rep_i in range(count):
            out.append(
                expand_template_with_hygiene(
                    elem, bindings, binding_symbols, introduced, index=rep_i))
        i += 2

    return build_proper(*out)


def apply_syntax_rules_hygiene(template, bindings, find_compiled):
    """
    Run binding-symbol discovery and template expansion with hygiene in one
    step. Equivalent to ``collect_binding_symbols`` then
    ``expand_template_with_hygiene`` with a fresh ``introduced`` map.
    """

    syms = collect_binding_symbols(template, find_compiled)
    return expand_template_with_hygiene(template, bindings, syms, {})


# The end.
