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

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from dataclasses import dataclass
from typing import Mapping, Tuple, Union

from koskari.exceptions import CompilerError, SpecialError
from koskari.lib import ABSENT, cons, gensym, nil
from koskari.types import build_proper, is_nil, is_pair, is_symbol, unpack


__all__ = (
    "Hygiene",
    "introduced_symbols_from_match",
    "scope_region_targets",
    "binder_recurse_targets",
    "collect_binding_symbols",
    "expand_template_with_hygiene",
    "apply_syntax_rules_hygiene",
)


@dataclass(frozen=True)
class Hygiene(object):
    """
    Freeze-time hygiene registration parallel to matcher composites.

    Slot keys align with matcher capture slot names plus composite binder
    registrations.

    :param binders: declared binder slots mapped to metadata entries
        containing kind and scopes
    :param scope_regions: REST and related scope slots mapped to kind info
    :param version: schema version
    """

    binders: Mapping[str, Mapping[str, Union[str, Tuple[str, ...]]]]
    scope_regions: Mapping[str, Mapping[str, str]]
    version: int = 1


    def is_empty(self):
        """
        True when no binder or scope-region registrations exist
        """

        return not self.binders and not self.scope_regions


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


    def binder_kinds(self):
        """
        Map binder slot names to their composite kind strings
        """

        out = {}
        for k, v in self.binders.items():
            kind = v.get("kind")
            if kind is not None:
                out[k] = kind
        return out


def _scope_region_slot_keys(hygiene):
    """
    Slot names that denote bodies or explicit binder scope references
    """

    keys = set(hygiene.scope_regions.keys())
    for ent in hygiene.binders.values():
        for ref in ent.get("scopes", ()):
            keys.add(ref)
    return keys


def _extract_define(matched, slot, _entry):
    """
    DEFINE binder introduces exactly one symbol into captures[slot]
    """

    val = matched.get(slot)
    if val is not None and val is not ABSENT and is_symbol(val):
        yield val


def _extract_decl(matched, slot, _entry):
    """
    DECL binder exposes captures[slot] plus inner slots such as name
    """

    name = matched.get("name")
    if name is not None and name is not ABSENT and is_symbol(name):
        yield name
        return
    raw = matched.get(slot)
    if raw is not None and is_symbol(raw):
        yield raw


def _extract_binding(matched, _slot, _entry):
    """
    BINDING binder exposes captures[name] when present
    """

    name = matched.get("name")
    if name is not None and name is not ABSENT and is_symbol(name):
        yield name


def _extract_bindings(matched, slot, _entry):
    """
    BINDINGS binder exposes frames list each carrying name and expr
    """

    cells = matched.get(slot)
    if cells is nil or cells is None:
        return
    if not isinstance(cells, list):
        return
    for fr in cells:
        if not isinstance(fr, dict):
            continue
        n = fr.get("name")
        if n is not None and is_symbol(n):
            yield n


def _extract_formals(matched, slot, _entry):
    """
    FORMALS binder introduces positional names, star names, star-star names,
    and bare-symbol formals shape
    """

    raw = matched.get(slot)
    if raw is not None and raw is not ABSENT and is_symbol(raw):
        yield raw
    pos = matched.get("positionals")
    if isinstance(pos, list):
        for fr in pos:
            if isinstance(fr, dict):
                n = fr.get("name")
                if n is not None and n is not ABSENT and is_symbol(n):
                    yield n
    star = matched.get("star")
    if star is not None and star is not ABSENT and is_symbol(star):
        yield star
    ss = matched.get("starstar")
    if ss is not None and ss is not ABSENT and is_symbol(ss):
        yield ss
    kwonly = matched.get("kwonly")
    if isinstance(kwonly, list):
        for fr in kwonly:
            if not isinstance(fr, dict):
                continue
            n = fr.get("name")
            if n is not None and is_symbol(n):
                yield n


def _extract_using(matched, _slot, _entry):
    """
    USING binder optionally binds captures[name]
    """

    name = matched.get("name")
    if name is not None and name is not ABSENT and is_symbol(name):
        yield name


_BINDER_EXTRACTORS = {
    "DEFINE": _extract_define,
    "DECL": _extract_decl,
    "BINDING": _extract_binding,
    "BINDINGS": _extract_bindings,
    "FORMALS": _extract_formals,
    "USING": _extract_using,
}


def introduced_symbols_from_match(matched, hygiene):
    """
    Collect symbols introduced by binder composites under hygiene rules.

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: compiled hygiene plan or None
    :rtype: set of koskari symbols
    """

    out = set()
    if matched is None or hygiene is None or hygiene.is_empty():
        return out

    for slot, entry in hygiene.binders.items():
        kind = entry.get("kind")
        fn = _BINDER_EXTRACTORS.get(kind)
        if not fn:
            continue
        for sym in fn(matched, slot, entry):
            if sym is not None and is_symbol(sym):
                out.add(sym)

    return out


def _recurse_define(_matched, _slot, _entry):
    return ()


def _recurse_decl(matched, _slot, _entry):
    out = []
    t = matched.get("type")
    if t is not None and t is not ABSENT:
        out.append(t)
    dt = matched.get("decl_tail")
    if dt is not None and dt is not ABSENT and is_pair(dt):
        out.append(dt)
    return out


def _recurse_binding(matched, _slot, _entry):
    ex = matched.get("expr")
    if ex is None or ex is ABSENT:
        return ()
    return (ex,)


def _recurse_bindings(matched, slot, _entry):
    cells = matched.get(slot)
    if cells is nil or cells is None:
        return ()
    if not isinstance(cells, list):
        return ()
    out = []
    for fr in cells:
        if isinstance(fr, dict):
            ex = fr.get("expr")
            if ex is not None and ex is not ABSENT:
                out.append(ex)
    return tuple(out)


def _recurse_formals(matched, _slot, _entry):
    out = []
    defs = matched.get("defaults")
    if isinstance(defs, list):
        for fr in defs:
            if isinstance(fr, dict):
                dv = fr.get("default")
                if dv is not None:
                    out.append(dv)
    kwonly = matched.get("kwonly")
    if isinstance(kwonly, list):
        for fr in kwonly:
            if isinstance(fr, dict):
                dv = fr.get("default")
                if dv is not None:
                    out.append(dv)
    pos = matched.get("positionals")
    if isinstance(pos, list):
        for fr in pos:
            if isinstance(fr, dict):
                attrs = fr.get("attrs")
                if isinstance(attrs, list):
                    for afr in attrs:
                        if isinstance(afr, dict):
                            av = afr.get("attr_val")
                            if av is not None:
                                out.append(av)
    return tuple(out)


def _recurse_using(matched, _slot, _entry):
    ex = matched.get("expr")
    if ex is None or ex is ABSENT:
        return ()
    return (ex,)


_RECURSE_EXTRACTORS = {
    "DEFINE": _recurse_define,
    "DECL": _recurse_decl,
    "BINDING": _recurse_binding,
    "BINDINGS": _recurse_bindings,
    "FORMALS": _recurse_formals,
    "USING": _recurse_using,
}


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

    for slot, entry in hygiene.binders.items():
        kind = entry.get("kind")
        fn = _RECURSE_EXTRACTORS.get(kind)
        if not fn:
            continue
        for form in fn(matched, slot, entry):
            if form is not None:
                yield form


def _norm_form_for_collect(form):
    """
    Wrap a single template atom as a one-element spine for recursion
    """

    return form if is_pair(form) else cons(form, nil)


def _iter_scope_region_forms(matched, hygiene):
    """
    Yield template-shaped roots referenced by scope-region slots
    """

    if matched is None or hygiene is None or hygiene.is_empty():
        return

    for sk in _scope_region_slot_keys(hygiene):
        if sk not in matched:
            continue
        val = matched[sk]
        if val is None or val is ABSENT:
            continue
        if isinstance(val, tuple):
            for elem in val:
                yield _norm_form_for_collect(elem)
        elif is_pair(val):
            for elem in unpack(val):
                yield _norm_form_for_collect(elem)
        else:
            yield _norm_form_for_collect(val)


def scope_region_targets(matched, hygiene):
    """
    Enumerate scope-region bodies without descending into them (same roots
    ``collect_binding_symbols`` visits under scope slots).

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: compiled hygiene plan or None
    """

    for form in _iter_scope_region_forms(matched, hygiene):
        yield form


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    if is_pair(expr):
        for elem in unpack(expr):
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


def collect_binding_symbols(template, find_compiled):
    """
    Walk a template form and collect symbols that participate in syntax-rules
    hygiene using explicit Hygiene plans.

    ``find_compiled(head)`` must return an object exposing ``hygiene`` and
    ``match_for_hygiene(rest)`` as described in the matcher-ng migration plan.

    :param template: Koskari pair tree head or leaf
    :param find_compiled: callable mapping head symbols to compiled specials
    """

    out = set()
    if template is nil or not is_pair(template):
        return out

    head, rest = template
    if not is_symbol(head):
        if rest is nil and is_pair(head):
            return collect_binding_symbols(head, find_compiled)
        for elem in rest.unpack():
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    special = find_compiled(head)
    hygiene = getattr(special, "hygiene", None)
    if special is None or hygiene is None:
        for elem in rest.unpack():
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    matched = None
    try:
        matched = special.match_for_hygiene(rest)
    except SpecialError:
        matched = None

    if matched is None:
        for elem in rest.unpack():
            out |= collect_binding_symbols(
                elem if is_pair(elem) else cons(elem, nil),
                find_compiled)
        return out

    out |= introduced_symbols_from_match(matched, hygiene)

    for form in _iter_scope_region_forms(matched, hygiene):
        out |= collect_binding_symbols(form, find_compiled)

    for form in binder_recurse_targets(matched, hygiene):
        out |= collect_binding_symbols(
            form if is_pair(form) else cons(form, nil),
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
