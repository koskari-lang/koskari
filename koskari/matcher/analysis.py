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
koskari.matcher.analysis

The analysis of a pattern's tables, the (slots, externs, consts, nodes,
entry) tuple TableBuilder.lower produces, for the emitter.

One pass of the analysis gives, per node, the static widths, the
suffix width on the node's cursor, the capture scope and slot indices,
the slot sets of ONEOF and OPTIONAL branches, the scope and index each
SAME reads, the column slots of an uncaptured REPEAT and the length
prechecks; and refuses at build the patterns the core does not lower.
Every child index in the node table is lower than its parent's, so
widths and slot sets are computed in ascending index order and the
cursor and scope facts a parent hands its children in descending
order, without recursion

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from dataclasses import dataclass
from typing import Optional

from koskari.compile.compiler import _is_const


__all__ = (
    'FIELD_LIMIT',
    'UNBOUNDED',
    'Analysis',
    'Scope',
    'analyse',
    'arm_gate',
)


# the largest width, count or slot count an instruction's 16-bit RAW
# field holds; 0xFFFF is the unbounded max of MATCH_FITS
FIELD_LIMIT = 0xFFFE

# the max of MATCH_FITS for no maximum
UNBOUNDED = 0xFFFF

# the kinds that test one element on their cursor
ELEMENT_KINDS = ('TEST', 'SAME', 'FORM', 'APP', 'CLASS', 'MAP')

# the kinds whose sub-pattern is a single value on a cursor of its own
VALUE_KINDS = ('APP', 'MAP_ENTRY_CONST', 'MAP_ENTRY_EXTERN')

FIELD_KINDS = ('FIELD_POS', 'FIELD_KW')

# the kinds whose children run on the node's own cursor
SAME_CURSOR_KINDS = ('MATCH', 'GROUP', 'ONEOF', 'OPTIONAL', 'CAPTURE')


@dataclass(frozen=True)
class Scope:
    """
    One capture scope, by kind: 'root'; 'group', a captured GROUP,
    whose captures the CAPTURE stores; 'discard', any other GROUP that
    is not a REPEAT body, whose captures are dropped when it closes;
    'pass', the passes of a REPEAT, whose owner is the REPEAT node, also
    when its body is a GROUP. names holds one entry per slot name, in
    match order; a slot's index in the scope is its name's position
    """

    owner: Optional[int]
    kind: str
    parent: Optional[int]
    depth: int
    names: tuple


@dataclass(frozen=True)
class Analysis:
    """
    The facts the emitter reads, per node index unless stated. A max
    width of None is unbounded.

    :ivar widths: (min, max) elements the node consumes on its cursor;
        None for a FIELD or MAP entry, which is not on a cursor
    :ivar fixed: whether min equals max; None where widths is None
    :ivar suffix: (min, max) width of what follows the node on its
        cursor, out to the cursor's owner: the FORM, the APP, FIELD or
        MAP value, the root, or for a REPEAT body the one pass. A
        REPEAT's w is its suffix min; a REST's w is its suffix, which
        is fixed. None where widths is None
    :ivar fits: node to the (min, max) of its MATCH_FITS precheck on
        the cursor it is on: a MATCH, GROUP, ONEOF branch or OPTIONAL
        sub-pattern of minimum width above 1 on its own cursor, and a
        ONEOF branch or OPTIONAL sub-pattern of minimum width 1 that a
        run of minimum width above 0 follows, with no max; omitted where
        a check already made at the same position implies the bounds
    :ivar form_fits: FORM node to the (min, max) checked on the cursor
        it descends to
    :ivar root_fits: the standalone root's (min, max), None for an arm
    :ivar scopes: the capture scopes, the root at index 0
    :ivar node_scope: the scope a node's captures store into, the
        scope it opens for a GROUP or a REPEAT body
    :ivar pass_scope: REPEAT node to its pass scope
    :ivar group_scope: GROUP node to the 'group' or 'discard' scope
        it opens; a GROUP that is a REPEAT body is in pass_scope
    :ivar slot_scope: per table slot, the scope it stores into
    :ivar slot_index: per table slot, its index in that scope
    :ivar branch_slots: each ONEOF branch node and OPTIONAL sub-pattern
        node to the sorted indices it can bind in the enclosing scope
    :ivar same: SAME node to the (scope, index) it reads, or None
        when no scope at its depth binds the name, a SAME that always
        misses
    :ivar columns: uncaptured REPEAT whose passes bind slots, to the
        enclosing scope's index for each name of its pass scope, in
        the pass scope's order
    """

    widths: tuple
    fixed: tuple
    suffix: tuple
    fits: dict
    form_fits: dict
    root_fits: Optional[tuple]
    scopes: tuple
    node_scope: tuple
    pass_scope: dict
    group_scope: dict
    slot_scope: tuple
    slot_index: tuple
    branch_slots: dict
    same: dict
    columns: dict


def analyse(tables, arm=False):
    """
    Analyse a pattern's tables for the core's emitter, refusing a
    pattern the core does not lower

    :param tables: (slots, externs, consts, nodes, entry) as
        TableBuilder.lower returns them
    :param arm: whether the root is a match arm's single value rather
        than a standalone matcher's sequence
    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    :rtype: Analysis
    """

    slots, _externs, consts, nodes, entry = tables

    check_constants(nodes, consts)
    widths = node_widths(nodes)
    check_shapes(nodes, widths, entry, arm)
    suffix, fits, form_fits, root_fits = cursor_layout(
        nodes, widths, entry, arm)
    scopes, node_scope, pass_scope, group_scope, slot_scope, captured = \
        capture_scopes(nodes, slots, entry)

    indices = [
        {name: index for index, name in enumerate(scope.names)}
        for scope in scopes]
    slot_index = tuple(
        None if scope is None else indices[scope][name]
        for name, scope in zip(slots, slot_scope))

    columns = {}
    for repeat, scope in pass_scope.items():
        if repeat in captured or not scopes[scope].names:
            continue
        outer = indices[node_scope[repeat]]
        columns[repeat] = tuple(outer[name] for name in scopes[scope].names)

    branch_slots = node_branch_slots(
        nodes, slots, scopes, indices, node_scope, pass_scope, slot_scope,
        captured)
    same = resolve_same(nodes, slots, scopes, indices, node_scope)

    return Analysis(
        widths=tuple(widths),
        fixed=tuple(None if w is None else w[0] == w[1] for w in widths),
        suffix=tuple(suffix),
        fits=fits,
        form_fits=form_fits,
        root_fits=root_fits,
        scopes=scopes,
        node_scope=tuple(node_scope),
        pass_scope=pass_scope,
        group_scope=group_scope,
        slot_scope=tuple(slot_scope),
        slot_index=slot_index,
        branch_slots=branch_slots,
        same=same,
        columns=columns)


def check_constants(nodes, consts):
    """
    Refuse a LITERAL value or a MAP entry key that the constant pool
    does not hold

    :raises TypeError: naming the node
    """

    for i, node in enumerate(nodes):
        if node[:2] == ('TEST', 'LITERAL'):
            what = "literal"
            value = consts[node[2]]
        elif node[0] == 'MAP_ENTRY_CONST':
            what = "mapping key"
            value = consts[node[1]]
        else:
            continue
        if not _is_const(value):
            raise TypeError(
                f"matcher {what} at node {i} is not a constant:"
                f" {type(value).__name__}")


def node_children(node):
    """
    The child node indices of a node tuple, in order
    """

    kind = node[0]
    if kind in ('MATCH', 'GROUP', 'ONEOF', 'MAP'):
        return node[1:]
    if kind == 'CLASS':
        return node[2:]
    if kind in ('FORM', 'OPTIONAL', 'NOT', 'REPEAT') or kind in FIELD_KINDS:
        return node[1:2]
    if kind == 'CAPTURE' or kind in VALUE_KINDS:
        return node[2:3] if node[2] != -1 else ()
    return ()


def add_widths(left, right):
    """
    The (min, max) width of two runs one after the other
    """

    if left[1] is None or right[1] is None:
        return (left[0] + right[0], None)
    return (left[0] + right[0], left[1] + right[1])


def node_widths(nodes):
    """
    The (min, max) width of every node, max None when unbounded, and
    None for a FIELD or MAP entry

    :raises ValueError: for a node tuple of an unknown kind
    """

    widths = []
    for i, node in enumerate(nodes):
        kind = node[0]
        if kind in ELEMENT_KINDS:
            width = (1, 1)
        elif kind == 'CAPTURE':
            width = (1, 1) if node[2] == -1 else widths[node[2]]
        elif kind == 'NOT':
            width = (0, 0)
        elif kind == 'REST':
            width = (node[1], None)
        elif kind in ('MATCH', 'GROUP'):
            width = (0, 0)
            for child in node[1:]:
                width = add_widths(width, widths[child])
        elif kind == 'ONEOF':
            branches = [widths[child] for child in node[1:]]
            highs = [high for _low, high in branches]
            width = (
                min(low for low, _high in branches),
                None if None in highs else max(highs))
        elif kind == 'REPEAT':
            width = (node[2] * widths[node[1]][0], None)
        elif kind == 'OPTIONAL':
            width = (0, widths[node[1]][1])
        elif kind in FIELD_KINDS or kind in ('MAP_ENTRY_CONST',
                                             'MAP_ENTRY_EXTERN'):
            width = None
        else:
            raise ValueError(f"unknown matcher node kind {kind!r} at node {i}")
        widths.append(width)
    return widths


def check_shapes(nodes, widths, entry, arm):
    """
    Refuse the shapes the widths alone decide: a REPEAT whose body may
    consume nothing, a count past FIELD_LIMIT, a FIELD value not one
    element wide, an APP or MAP value or an arm root whose width range
    does not include 1, a positional FIELD after a keyword FIELD, and a
    SAME in a match arm, whose root captures are variables, not slots
    of a mark

    :raises ValueError: naming the node
    """

    for i, node in enumerate(nodes):
        kind = node[0]
        if kind == 'REPEAT':
            if widths[node[1]][0] < 1:
                raise ValueError(
                    f"REPEAT at node {i} has a body that may consume"
                    f" nothing (minimum width {widths[node[1]][0]})")
            if node[2] > FIELD_LIMIT:
                raise ValueError(
                    f"REPEAT at node {i} has min_count {node[2]}, past"
                    f" {FIELD_LIMIT}")
        elif kind == 'REST':
            if node[1] > FIELD_LIMIT:
                raise ValueError(
                    f"REST at node {i} has min_tail {node[1]}, past"
                    f" {FIELD_LIMIT}")
        elif kind in FIELD_KINDS:
            if widths[node[1]] != (1, 1):
                raise ValueError(
                    f"FIELD at node {i} has a sub-pattern of width"
                    f" {widths[node[1]]}, not exactly 1")
        elif kind in VALUE_KINDS:
            sub = node[2]
            if sub != -1:
                low, high = widths[sub]
                if low > 1 or high == 0:
                    raise ValueError(
                        f"{kind} at node {i} has a value sub-pattern of"
                        f" width {widths[sub]}, which does not include 1")
        elif kind == 'CLASS':
            saw_keyword = False
            for child in node[2:]:
                if nodes[child][0] == 'FIELD_KW':
                    saw_keyword = True
                elif saw_keyword:
                    raise ValueError(
                        f"CLASS at node {i} has positional FIELD {child}"
                        f" after a keyword FIELD")

    if arm:
        for i, node in enumerate(nodes):
            if node[0] == 'SAME':
                raise ValueError(
                    f"SAME at node {i} is in a match arm, which does not"
                    f" lower a back-reference")
        low, high = widths[entry]
        if low > 1 or high == 0:
            raise ValueError(
                f"match arm root at node {entry} has width"
                f" {widths[entry]}, which does not include 1")


def fits_bounds(i, width, after, open_end):
    """
    The (min, max) a MATCH_FITS checks for a run of width with after
    following it on the cursor; max None when unbounded, as it is on a
    REPEAT pass's cursor, whose further passes are unbounded

    :raises ValueError: when a bound is past FIELD_LIMIT
    """

    low = width[0] + after[0]
    if open_end or width[1] is None or after[1] is None:
        high = None
    else:
        high = width[1] + after[1]
    if low > FIELD_LIMIT or (high is not None and high > FIELD_LIMIT):
        raise ValueError(
            f"length bounds ({low}, {high}) at node {i} are past"
            f" {FIELD_LIMIT}")
    return (low, high)


def arm_gate(tables):
    """
    The test a match arm applies first, in place, to its subject, when
    it is a type test: ('CORE', ordinal), or ('TYPE', extern name) for
    a TYPE or a CLASS on an extern, CLASS testing the type before
    anything else. None when the arm starts with any other test

    :param tables: the arm's (slots, externs, consts, nodes, entry)
    """

    _slots, externs, _consts, nodes, i = tables
    while True:
        node = nodes[i]
        if node[0] == 'MATCH' and len(node) == 2:
            i = node[1]
        elif node[0] == 'CAPTURE' and node[2] != -1:
            i = node[2]
        else:
            break
    if node[0] == 'CLASS':
        return ('TYPE', externs[node[1]])
    if node[0] == 'TEST' and node[1] == 'CORE':
        return ('CORE', int(node[2]))
    if node[0] == 'TEST' and node[1] == 'TYPE':
        return ('TYPE', externs[node[2]])
    return None


def cursor_layout(nodes, widths, entry, arm):
    """
    The suffix of every node on its cursor and the length prechecks,
    handed from each node to its children. Refuses a CAPTURE in a NOT
    subtree, a REST followed on its cursor by anything of variable
    width, and a w past FIELD_LIMIT

    :return: (suffix per node, fits, form_fits, root_fits)
    :raises ValueError: naming the node
    """

    count = len(nodes)
    # per node: (suffix, open_end, checked, under_not), set by the
    # parent; open_end marks a REPEAT pass's cursor, checked the bounds
    # a precheck already tested at the node's position
    placed = [None] * count
    suffix = [None] * count
    fits = {}
    form_fits = {}

    # per node, whether it holds a REST on its own cursor; per placed
    # node, whether a REST follows it on its cursor
    holds_rest = []
    for node in nodes:
        holds_rest.append(node[0] == 'REST' or (
            node[0] in SAME_CURSOR_KINDS
            and any(holds_rest[child] for child in node_children(node))))
    rest_after = {}

    root_fits = None
    if not arm:
        root_fits = fits_bounds(entry, widths[entry], (0, 0), False)
    placed[entry] = ((0, 0), False, root_fits, False)
    # the ONEOF branches and OPTIONAL sub-patterns: an alternative
    # that consumes an element the suffix needs at its minimum is not
    # taken
    branches = set()

    for i in range(entry, -1, -1):
        if placed[i] is None:
            continue
        after, open_end, checked, under_not = placed[i]
        suffix[i] = after
        node = nodes[i]
        kind = node[0]
        in_not = under_not or kind == 'NOT'

        if kind == 'CAPTURE' and under_not:
            raise ValueError(f"CAPTURE at node {i} is in a NOT subtree")
        if kind == 'REST' and rest_after.get(i):
            raise ValueError(
                f"REST at node {i} is followed on its cursor by another"
                f" REST: 2 REST on one cursor")
        if kind == 'REST' and after[0] != after[1]:
            raise ValueError(
                f"REST at node {i} is followed on its cursor by a run of"
                f" variable width {after}")
        if kind in ('REST', 'REPEAT') and after[0] > FIELD_LIMIT:
            raise ValueError(
                f"{kind} at node {i} has a suffix width {after[0]}, past"
                f" {FIELD_LIMIT}")

        bounds = None
        if (kind in ('MATCH', 'GROUP') or i in branches) \
                and widths[i][0] > 1:
            bounds = fits_bounds(i, widths[i], after, open_end)
        elif i in branches and widths[i][0] == 1 and after[0] > 0:
            # the element the alternative pulls, and the suffix's
            # minimum after it
            bounds = fits_bounds(i, widths[i], after, True)
        # a check already made at this position that implies the bounds
        # (at least the minimum, at most a bounded maximum) makes them
        # redundant
        implied = checked is not None and bounds is not None and (
            checked[0] >= bounds[0]
            and (bounds[1] is None
                 or (checked[1] is not None and checked[1] <= bounds[1])))
        if bounds is not None and not implied:
            fits[i] = bounds
            checked = bounds

        if kind in ('MATCH', 'GROUP'):
            children = node[1:]
            rested = rest_after.get(i, False)
            for k in range(len(children) - 1, -1, -1):
                child = children[k]
                placed[child] = (
                    after, open_end, checked if k == 0 else None, in_not)
                rest_after[child] = rested
                after = add_widths(widths[child], after)
                rested = rested or holds_rest[child]
        elif kind == 'ONEOF':
            for child in node[1:]:
                branches.add(child)
                placed[child] = (after, True, checked, in_not)
                rest_after[child] = rest_after.get(i, False)
        elif kind in ('CAPTURE', 'OPTIONAL'):
            for child in node_children(node):
                if kind == 'OPTIONAL':
                    branches.add(child)
                placed[child] = (
                    after, open_end or kind == 'OPTIONAL', checked, in_not)
                rest_after[child] = rest_after.get(i, False)
        elif kind in ('NOT', 'REPEAT'):
            # a REPEAT pass's bound cursor, or the copy of the cursor
            # without its reserve that a NOT looks ahead on: nothing
            # follows the sub-pattern there
            placed[node[1]] = ((0, 0), True, None, in_not)
        elif kind == 'FORM':
            sub = node[1]
            bounds = fits_bounds(i, widths[sub], (0, 0), False)
            form_fits[i] = bounds
            placed[sub] = ((0, 0), False, bounds, in_not)
        elif kind in ('CLASS', 'MAP'):
            for child in node_children(node):
                placed[child] = (None, False, None, in_not)
        else:
            # a FIELD or MAP entry value, or an APP result: one value
            # on a cursor of its own
            for child in node_children(node):
                placed[child] = ((0, 0), False, None, in_not)

    return suffix, fits, form_fits, root_fits


def capture_scopes(nodes, slots, entry):
    """
    The capture scopes and the scope of every node and slot. A
    captured GROUP opens a scope, as does a REPEAT for its passes (a
    GROUP that is a REPEAT's body is that pass scope), and any other
    GROUP opens a scope whose captures are discarded. The names of an
    uncaptured REPEAT's pass scope are also names of its enclosing
    scope, which holds their per-pass value lists. A name binds at
    most once in a scope, except across the alternatives of one ONEOF,
    whose bindings share the name's one index

    :return: (scopes, node_scope, pass_scope, group_scope, slot_scope,
        the set of REPEAT nodes directly under a CAPTURE)
    :raises ValueError: when a name binds twice in one scope, or a
        scope holds more than FIELD_LIMIT names
    """

    node_scope = [None] * len(nodes)
    slot_scope = [None] * len(slots)
    parent_node = [None] * len(nodes)
    owners = [None]
    kinds = ['root']
    parents = [None]
    depths = [0]
    pass_scope = {}
    group_scope = {}
    captured = set()

    def open_scope(owner, kind, parent):
        opened = len(owners)
        owners.append(owner)
        kinds.append(kind)
        parents.append(parent)
        depths.append(depths[parent] + 1)
        return opened

    if nodes[entry][0] == 'GROUP':
        node_scope[entry] = open_scope(entry, 'discard', 0)
        group_scope[entry] = node_scope[entry]
    else:
        node_scope[entry] = 0

    for i in range(entry, -1, -1):
        scope = node_scope[i]
        if scope is None:
            continue
        node = nodes[i]
        kind = node[0]
        for child in node_children(node):
            parent_node[child] = i
        if kind == 'REPEAT':
            opened = open_scope(i, 'pass', scope)
            pass_scope[i] = opened
            node_scope[node[1]] = opened
            continue
        if kind == 'CAPTURE':
            slot_scope[node[1]] = scope
            if node[2] != -1 and nodes[node[2]][0] == 'REPEAT':
                captured.add(node[2])
        for child in node_children(node):
            if nodes[child][0] != 'GROUP':
                node_scope[child] = scope
            elif kind == 'CAPTURE':
                node_scope[child] = open_scope(child, 'group', scope)
                group_scope[child] = node_scope[child]
            else:
                node_scope[child] = open_scope(child, 'discard', scope)
                group_scope[child] = node_scope[child]

    # each scope's names, keyed to the first slot index that binds
    # them, which is their match order, and the nodes that bind each:
    # a CAPTURE, or an uncaptured REPEAT for its columns
    first = [{} for _owner in owners]
    binders = {}
    for i, node in enumerate(nodes):
        if node[0] == 'CAPTURE' and node_scope[i] is not None:
            name = slots[node[1]]
            first[node_scope[i]].setdefault(name, node[1])
            binders.setdefault((node_scope[i], name), []).append(i)

    # an inner REPEAT has the lower node index, so its names reach its
    # pass scope's names before that scope's own REPEAT is visited
    for repeat in sorted(pass_scope):
        if repeat in captured:
            continue
        scope = node_scope[repeat]
        outer = first[scope]
        for name, index in first[pass_scope[repeat]].items():
            if outer.get(name, index) >= index:
                outer[name] = index
            binders.setdefault((scope, name), []).append(repeat)

    for (scope, name), bound in binders.items():
        check_single_binding(nodes, parent_node, name, bound)

    scopes = []
    for scope, names in enumerate(first):
        if len(names) > FIELD_LIMIT:
            raise ValueError(
                f"capture scope of node {owners[scope]} has {len(names)}"
                f" slot names, past {FIELD_LIMIT}")
        scopes.append(Scope(
            owner=owners[scope],
            kind=kinds[scope],
            parent=parents[scope],
            depth=depths[scope],
            names=tuple(sorted(names, key=names.get))))

    return (
        tuple(scopes), node_scope, pass_scope, group_scope, slot_scope,
        captured)


def check_single_binding(nodes, parent_node, name, bound):
    """
    Refuse a name bound twice in one scope: for every two of the
    binding nodes, the nearest node above both must be a ONEOF with
    each under a different alternative

    :raises ValueError: naming both nodes and the name
    """

    for k, one in enumerate(bound):
        # each node from one up to the root, to the child of it on the
        # way from one; None for one itself
        above = {one: None}
        node = one
        while parent_node[node] is not None:
            above[parent_node[node]] = node
            node = parent_node[node]
        for other in bound[k + 1:]:
            child = None
            node = other
            while node is not None and node not in above:
                child = node
                node = parent_node[node]
            if node is None or nodes[node][0] != 'ONEOF' or child is None \
                    or above[node] is None or above[node] == child:
                raise ValueError(
                    f"slot name {name!r} is bound twice in one scope,"
                    f" at node {min(one, other)} and node"
                    f" {max(one, other)}")


def node_branch_slots(
        nodes,
        slots,
        scopes,
        indices,
        node_scope,
        pass_scope,
        slot_scope,
        captured):
    """
    The indices each ONEOF branch and OPTIONAL sub-pattern can bind in
    the scope of its ONEOF or OPTIONAL: its CAPTUREs that store there
    and the column slots of the uncaptured REPEATs under it whose
    enclosing scope it is

    :return: branch node to its sorted tuple of indices
    """

    # per node, the (scope, name) pairs its subtree can bind
    binds = []
    for i, node in enumerate(nodes):
        found = set()
        for child in node_children(node):
            found |= binds[child]
        if node[0] == 'CAPTURE':
            found.add((slot_scope[node[1]], slots[node[1]]))
        elif node[0] == 'REPEAT' and i not in captured:
            outer = node_scope[i]
            found.update(
                (outer, name) for name in scopes[pass_scope[i]].names)
        binds.append(found)

    branch_slots = {}
    for i, node in enumerate(nodes):
        if node[0] not in ('ONEOF', 'OPTIONAL') or node_scope[i] is None:
            continue
        scope = node_scope[i]
        for branch in node[1:]:
            branch_slots[branch] = tuple(sorted(
                indices[scope][name]
                for bound, name in binds[branch] if bound == scope))
    return branch_slots


def resolve_same(nodes, slots, scopes, indices, node_scope):
    """
    The (scope, index) each SAME reads: its depth counts the open
    scopes from its own, and of the scopes at that depth the innermost
    that binds the slot's name somewhere in the pattern gives the
    slot; a SAME read before the slot is bound is accepted. The pass
    scope of a REPEAT whose body is not a GROUP is not a depth of its
    own: it shares the depth of the scope around the REPEAT, as
    master's frames do. A SAME none of whose scopes at its depth binds
    the name, or whose depth passes the root, maps to None and always
    misses, as on master
    """

    same = {}
    for i, node in enumerate(nodes):
        if node[0] != 'SAME' or node_scope[i] is None:
            continue
        name = slots[node[1]]
        depth = node[2]
        scope = node_scope[i]
        level = 0
        found = None
        while scope is not None:
            if level == depth and name in indices[scope]:
                found = scope
                break
            owner = scopes[scope].owner
            if scopes[scope].kind != 'pass' \
                    or nodes[nodes[owner][1]][0] == 'GROUP':
                level += 1
                if level > depth:
                    break
            scope = scopes[scope].parent
        same[i] = None if found is None else (found, indices[found][name])
    return same


# The end.
