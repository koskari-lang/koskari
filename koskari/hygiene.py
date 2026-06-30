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

Hygiene discovery and template expansion driven by freeze-built hygiene
trees aligned to matcher capture-scope structure, plus stable Syntax hooks
(:meth:`koskari.syntax.Syntax.match_for_hygiene`).

Capture-shape contract (runtime walk):

* Binder slot -> symbol, Datum, dict (GROUP), or list of dicts (REPEAT)
* Region slot (BODY/VALUE) -> pair spine, optional Datum wrapper (never nested),
  or special form
* group_child edge -> matched[slot] is dict (native GROUP capture)
* repeat_element edge -> matched[slot] is list of dict frames
* subform descent -> region value is a compile special; uses that form's arm plan

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from dataclasses import dataclass, field
from typing import Optional, Tuple, Union

from koskari.exceptions import CompilerError
from koskari.lib import (Datum, bracket_cons, bracket_nil, bracket_pair, cons,
                         gensym, nil)
from koskari.types import (build_proper, is_keyword, is_nil, is_pair,
                           is_symbol, unpack)


__all__ = (
    "HygieneEdge",
    "HygieneNode",
    "Hygiene",
    "validate_hygiene_tree",
    "introduced_symbols_from_match",
    "binder_recurse_targets",
    "collect_binding_symbols",
    "expand_template_with_hygiene",
    "apply_syntax_rules_hygiene",
    "finalize_macro_hygiene_unbox_one_pass",
    "walk_hygiene_tree",
)


# --- Hygiene tree types


@dataclass
class HygieneNode(object):
    """
    One capture-scope hygiene context from matcher lowering

    While :class:`koskari.matcher.HygienicMatchCompiler` builds a plan,
    binders and regions are dicts and scoped_slots is a set. Each node
    finalized onto :class:`Hygiene` uses copied dicts and a frozenset for
    scoped_slots

    :param node_id: unique node index in the owning tree
    :param parent_id: parent node id, or None for the root
    :param binders: binder slot names mapped to owning Pattern instances
    :param regions: scope-region slot names mapped to owning Pattern instances
    :param scoped_slots: region slots walked via binder ``_scopes`` only
    :param link_slot: GROUP link capture slot during lowering only; cleared on freeze
    """

    node_id: int
    parent_id: Optional[int]
    binders: dict = field(default_factory=dict)
    regions: dict = field(default_factory=dict)
    scoped_slots: Union[set, frozenset] = field(default_factory=set)
    link_slot: Optional[str] = field(default=None, compare=False, repr=False)


@dataclass(frozen=True)
class HygieneEdge(object):
    """
    Parent-to-child link in a hygiene tree

    :param kind: ``group_child`` or ``repeat_element``
    :param parent_id: parent node id
    :param child_id: child node id
    :param capture_slot: capture key in the parent matched mapping
    """

    kind: str
    parent_id: int
    child_id: int
    capture_slot: str


@dataclass(frozen=True)
class Hygiene(object):
    """
    Freeze-time hygiene plan: nodes mirror capture-scope nesting; edges
    encode GROUP and REPEAT child walks
    """

    nodes: Tuple[HygieneNode, ...]
    edges: Tuple[HygieneEdge, ...]
    root_id: int


    def is_empty(self):
        """
        True when the root node has no binders, regions, or outgoing edges
        """

        if self.edges:
            return False
        root = self.node_by_id(self.root_id)
        return not bool(root.binders or root.regions)


    def __bool__(self):
        return not self.is_empty()


    @property
    def root(self):
        """
        The root hygiene node
        """

        return self.node_by_id(self.root_id)


    def node_by_id(self, node_id):
        """
        Return the node for ``node_id``

        :param node_id: node index
        :rtype: HygieneNode
        :raises KeyError: when ``node_id`` is unknown
        """

        for node in self.nodes:
            if node.node_id == node_id:
                return node
        raise KeyError(node_id)


    def edges_from(self, parent_id):
        """
        Yield edges whose parent is ``parent_id``
        """

        for edge in self.edges:
            if edge.parent_id == parent_id:
                yield edge


    @classmethod
    def single_node(cls, binders=None, regions=None, scoped_slots=None):
        """
        Build a one-node tree (tests and synthetic plans)

        :param binders: optional binder slot map
        :param regions: optional region slot map
        :param scoped_slots: optional scoped region slot names
        :rtype: Hygiene
        """

        ss = frozenset(scoped_slots or ())
        node = HygieneNode(
            0, None,
            dict(binders or {}),
            dict(regions or {}),
            ss)
        return cls((node,), (), 0)


# --- Tree validation


def validate_hygiene_tree(tree):
    """
    Verify structural invariants of a hygiene tree. Raises ValueError on
    violation

    :param tree: Hygiene instance
    """

    if not tree.nodes:
        raise ValueError("hygiene tree has no nodes")

    ids = {n.node_id for n in tree.nodes}
    if len(ids) != len(tree.nodes):
        raise ValueError("duplicate hygiene node ids")

    if tree.root_id not in ids:
        raise ValueError("root_id not in nodes")

    for node in tree.nodes:
        if node.parent_id is not None and node.parent_id not in ids:
            raise ValueError(
                "node %d parent %r missing"
                % (node.node_id, node.parent_id))
        for slot in node.binders:
            if slot in node.regions:
                raise ValueError(
                    "slot %r is both binder and region on node %d"
                    % (slot, node.node_id))
        overlap = set(node.regions) & set(node.scoped_slots)
        if overlap:
            raise ValueError(
                "scoped_slots overlap regions on node %d: %r"
                % (node.node_id, overlap))

    for edge in tree.edges:
        if edge.parent_id not in ids or edge.child_id not in ids:
            raise ValueError(
                "edge %r references missing node" % (edge,))
        if edge.kind not in ("group_child", "repeat_element"):
            raise ValueError(
                "unknown edge kind %r" % (edge.kind,))


# --- Matcher capture walk (internal)


def _hygiene_collect_walk_roots(form):
    """
    Normalize matcher capture values into roots for binding-symbol walks.
    Scope-region captures are often Datum-wrapped spine slices; peel one
    layer for the walk only. Tuple captures expand to per-element roots
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


def _walk_region_roots(matched, owner, find_compiled, out):
    """
    Walk scope-region targets from ``owner`` into ``out``
    """

    for form in owner.iter_scope_targets(matched):
        for root in _hygiene_collect_walk_roots(form):
            out |= _collect_from_region_root(root, find_compiled)


def _walk_binder_scopes(matched, owner, find_compiled, out):
    """
    Walk binder ``_scopes`` sibling slots (same matched mapping)
    """

    scopes = getattr(owner, "_scopes", ())
    if not scopes:
        return
    for scope_slot in scopes:
        val = matched and matched.get(scope_slot)
        if val is None:
            continue
        if isinstance(val, Datum) and (is_pair(val.datum) or is_nil(val.datum)):
            spine = val.datum
        elif is_pair(val) or is_nil(val):
            spine = val
        else:
            out |= _collect_from_region_root(val, find_compiled)
            continue
        for root in _hygiene_collect_walk_roots(spine):
            out |= _collect_from_region_root(root, find_compiled)


def _collect_from_region_root(root, find_compiled):
    """
    Descend a scope-region root the same way as ``collect_binding_symbols``
    on a nested form: specials via their hygiene arm; other pair spines
    via element-wise recursion without harvesting arbitrary symbols

    A region capture may be a proper list of sibling forms
    ``(form1 . (form2 . nil))`` or one compile-special application
    ``(compile/name . args)``. Those shapes share cons structure; when the
    ``car`` is a resolved compile head we treat the spine as one application,
    otherwise we walk every element from ``unpack(root)`` so the first
    sibling is not dropped
    """

    # print("DEBUG _collect_from_region_root", "root=", repr(root))

    if isinstance(root, Datum):
        root = root.datum

    if not root or is_symbol(root) or not is_pair(root):
        return set()

    items = list(unpack(root))
    first = items[0]
    if is_symbol(first) and find_compiled(first) is not None:
        return collect_binding_symbols(root, find_compiled)

    out = set()
    for elem in items:
        out |= collect_binding_symbols(elem, find_compiled)
    return out


def walk_hygiene_tree(tree, node_id, matched, find_compiled, tracer=None):
    """
    Collect binding symbols by walking a hygiene tree against captures

    :param tree: Hygiene plan
    :param node_id: node to visit
    :param matched: capture dict (``MatchResult.captures``-shaped) for this node
    :param find_compiled: callable mapping head symbols to compiled specials
    :param tracer: optional dict with list counters (tests only)
    :rtype: set of symbols
    """

    if tree is None or matched is None:
        return set()

    assert isinstance(matched, dict)

    if tracer is not None:
        tracer.setdefault("node_enter", []).append(node_id)

    node = tree.node_by_id(node_id)
    out = set()

    for owner in node.binders.values():
        for sym in owner.iter_introduced_symbols(matched):
            if sym is not None and is_symbol(sym):
                out.add(sym)
        for form in owner.iter_recurse_targets(matched):
            if form is not None and not isinstance(form, Datum):
                out |= collect_binding_symbols(form, find_compiled)
        _walk_binder_scopes(matched, owner, find_compiled, out)
        if tracer is not None:
            scopes = getattr(owner, "_scopes", ())
            for scope_slot in scopes:
                tracer.setdefault("scope_walk", []).append(scope_slot)

    for slot, owner in node.regions.items():
        if slot in node.scoped_slots:
            continue
        if tracer is not None:
            tracer.setdefault("region_walk", []).append(slot)
        _walk_region_roots(matched, owner, find_compiled, out)

    for edge in tree.edges_from(node_id):
        val = matched and matched.get(edge.capture_slot)
        if edge.kind == "group_child":
            if isinstance(val, dict):
                out |= walk_hygiene_tree(
                    tree, edge.child_id, val, find_compiled, tracer)
        elif edge.kind == "repeat_element":
            frames = val
            if isinstance(frames, Datum):
                frames = frames.datum
            if is_nil(frames):
                continue
            if not isinstance(frames, list):
                continue
            for fr in frames:
                if isinstance(fr, dict):
                    if tracer is not None:
                        tracer.setdefault("repeat_element", []).append(
                            edge.capture_slot)
                    out |= walk_hygiene_tree(
                        tree, edge.child_id, fr, find_compiled, tracer)

    return out


def introduced_symbols_from_match(matched, hygiene):
    """
    Collect symbols introduced by binder composites under hygiene rules.

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: Hygiene plan or None
    :rtype: set of koskari symbols
    """

    if not isinstance(hygiene, Hygiene):
        return set()
    return walk_hygiene_tree(hygiene, hygiene.root_id, matched, lambda h: None)


def binder_recurse_targets(matched, hygiene):
    """
    Yield forms nested under binder slots that must be walked for nested
    binder discovery (initializer arms).

    :param matched: capture mapping from one successful hygiene-side match
    :param hygiene: Hygiene plan or None
    :rtype: iterable of forms suitable for collect_binding_symbols recursion
    """

    if matched is None or hygiene is None or hygiene.is_empty():
        return

    node = hygiene.node_by_id(hygiene.root_id)
    for owner in node.binders.values():
        for form in owner.iter_recurse_targets(matched):
            if form is not None and not isinstance(form, Datum):
                yield form


# --- Template expansion


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
    Peel a single outer ``Datum`` wrapper when present so list, pair, or tuple
    shape is visible to template expansion. The bindings dict still holds
    matcher captures unchanged elsewhere
    """

    return val.datum if isinstance(val, Datum) else val


def _repeat_count(template, bindings):
    syms = set()
    _collect_symbols(template, syms)
    counts = {
        len(v)
        for s in syms
        if s in bindings
        and isinstance(
            (v := _binding_value_for_expand(bindings[s])), (list, tuple))
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
        (often ``Datum``-wrapped); a single outer ``Datum`` wrapper is peeled
        only when expanding list or ellipsis structure, not when returning
        an unexpanded binding reference unchanged
    :param binding_symbols: set of symbols needing gensym when not bound
    :param introduced: optional map mutated from symbol to gensym
    :param index: ellipsis repetition slice index when repeating
    """

    if introduced is None:
        introduced = {}

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
            out.extend(unpack(spliced))
        else:
            count = _repeat_count(elem, bindings)
            for rep_i in range(count):
                out.append(
                    expand_template_with_hygiene(
                        elem, bindings, binding_symbols, introduced,
                        index=rep_i))
        i += 2

    # preserve the template's delimiter style: a bracket-pair template
    # rebuilds as a bracket list (terminated by bracket-nil), so a [ ] in a
    # syntax-rules template survives expansion instead of flattening to ( )
    if isinstance(template, bracket_pair):
        return bracket_cons(*out, bracket_nil) if out else bracket_nil

    return build_proper(*out)


# --- Macro output finalize


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
        if isinstance(c, Datum):
            first_pos = c.position
            c = c.datum
        if first_pos is not None:
            nx.position = first_pos
        nx.car = c
        return nx.car

    def peel_cdr_datums_in_place(nx):
        # Repeated Datum wrappers along the cdr chain (not Datum-in-Datum).
        tail = nx.cdr
        while isinstance(tail, Datum):
            tail = tail.datum
            nx.cdr = tail
            tail = nx.cdr
        return tail

    def walk_spine(nx):
        while not is_nil(nx):
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

    if isinstance(form, Datum):
        form = form.datum
    if is_symbol(form):
        if form in binding_symbols:
            return gensym_binding(form)
        return form
    if is_pair(form):
        walk_spine(form)
    return form


# --- Binding-symbol discovery


def collect_binding_symbols(node, find_compiled):
    """
    Walk one template or capture node for binding-symbol discovery.

    Only koskari pair trees participate; tuples are runtime/capture values,
    not source structure, and are never descended. Other atoms yield nothing.

    :param node: koskari pair, nil, tuple (ignored), or other leaf
    :param find_compiled: callable mapping head symbols to compiled specials
    :rtype: set of symbols
    """

    # print("DEBUG collect_binding_symbols", "node=", repr(node))

    out = set()

    if isinstance(node, Datum):
        return out

    if is_nil(node) or not is_pair(node):
        return out

    head, rest = node
    if not is_symbol(head):
        if is_nil(rest) and is_pair(head):
            return collect_binding_symbols(head, find_compiled)
        if is_keyword(head):
            for elem in rest.unpack():
                out |= collect_binding_symbols(elem, find_compiled)
            return out
        return _collect_from_region_root(node, find_compiled)

    special = find_compiled(head)
    # print("DEBUG collect_binding_symbols", "special=", repr(special))
    matched = None
    hygiene = None
    if special is not None:
        matched, _, hygiene = special.match_for_hygiene(node)
    # print("DEBUG collect_binding_symbols", "matched=", repr(matched))
    # print("DEBUG collect_binding_symbols", "hygiene=", repr(hygiene))
    if special is None or matched is None:
        for elem in rest.unpack():
            out |= collect_binding_symbols(elem, find_compiled)
        return out

    if hygiene:
        out |= walk_hygiene_tree(
            hygiene, hygiene.root_id, matched, find_compiled)

    else:
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
