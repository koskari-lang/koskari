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
koskari.match_core

The matcher core: the analysis of a pattern's tables, the (slots,
externs, consts, nodes, entry) tuple MatchCompiler.lower produces, and
the emitter that lowers them to the MATCH_* instructions.

One pass of the analysis gives, per node, the static widths, the
suffix width on the node's cursor, the capture scope and slot indices,
the slot sets of ONEOF and OPTIONAL branches, the scope and index each
SAME reads, the column slots of an uncaptured REPEAT and the length
prechecks; and refuses at build the patterns the core does not lower.
Every child index in the node table is lower than its parent's, so
widths and slot sets are computed in ascending index order and the
cursor and scope facts a parent hands its children in descending
order, without recursion.

The emitter walks the nodes from the root. A node on a cursor leaves
the stack as it found it on a hit. An element's pull is emitted apart
from its test: MATCH_NEXT on a cursor, nothing for a single value,
which is tested in place when its pattern is one element test or a
ONEOF of them. A test leaves its value on both exits, and the value's
last user removes it. A miss jumps from the stack as the failing
instruction leaves it, through the fail entry for its target and that
depth, which pops down to the target. A capture is stored into the
mark of its scope on the stack, except in a match arm, whose root
scope has no mark: a root capture is stored into its variable as soon
as its value is tested. An instruction names a cursor, mark or items
list by its depth under the top as the instruction finds the stack.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from dataclasses import dataclass
from functools import partial
from typing import Optional

from koskari.compile.compiler import _is_const
from koskari.lib import OpCode, symbol


__all__ = (
    'FIELD_LIMIT',
    'Analysis',
    'Emitter',
    'Scope',
    'analyse',
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
        MatchCompiler.lower returns them
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


class Emitter:
    """
    The emission of one matcher's walk into a target: the pattern's
    tables and analysis, the position of the top stack entry the walk
    pushed, the entry holding each open scope's mark, the POPs not yet
    emitted, the miss targets and the fail entries into them. A stack
    entry is named by its position from the bottom of the walk's
    entries, the first at 0; an instruction names it by its depth under
    the top, the top's position less the entry's. A miss target is a
    label the walk reaches with its top entry at a known position; a
    jump to it from a higher top goes to the fail entry for that
    (target, top), which pops down to the target and continues there.
    The fail entries are emitted after the walk, or one of them ahead
    of its target where the code before the target never falls
    through. In a match arm the root scope has no mark: its captures
    are stored into the arm's variables
    """

    def __init__(self, target, tables, atoms, arm=False):
        """
        :param target: a FunctionTarget or FragmentTarget
        :param tables: (slots, externs, consts, nodes, entry)
        :param atoms: the extern names mapped to the atoms the pattern
            wrote them as
        :param arm: whether the root is a match arm's single value
        :raises TypeError: when a LITERAL value or a MAP entry key is
            not a constant the pool holds
        :raises ValueError: when the pattern is one the core refuses
        """

        _slots, externs, consts, nodes, entry = tables
        self.target = target
        self.nodes = nodes
        self.consts = consts
        self.entry = entry
        self.externs = [atoms.get(name) or symbol(name) for name in externs]
        self.analysis = analyse(tables, arm=arm)
        self.frame = arm
        self.top = -1
        self.marks = {}
        self.pops = 0
        # per miss target: (home, base, cont), the top where the label
        # is reached, and the top and the label where its code goes on
        # past the POPs it opens with; cont None for the standalone
        # miss, which returns None
        self.targets = {}
        # (target, top) to the label of its fail entry, in the order
        # requested, and the labels of the entries already emitted
        self.entries = {}
        self.placed = set()
        # the CAPTURE over each captured node: a GROUP stores its own
        # captures, and a REPEAT or REST leaves its value for the store
        self.captured = {}
        for i, node in enumerate(nodes):
            if node[0] == 'CAPTURE' and node[2] != -1:
                self.captured[node[2]] = i


    def op(self, opcode, *fields, effect=0):
        """
        Emit one instruction, its EXTRA fields padded to four, and
        count its effect on the stack where it falls through. A POP is
        held and merged with the POPs that follow it
        """

        self.top += effect
        if opcode is OpCode.POP:
            self.pops += fields[0]
            return
        self.settle()
        if opcode.extra:
            arg = fields + (0,) * (4 - len(fields))
        else:
            arg = fields[0] if fields else 0
        self.target.instruction(opcode, arg)


    def settle(self):
        """
        Emit the POPs held since the last instruction
        """

        if self.pops:
            self.target.instruction(OpCode.POP, self.pops)
            self.pops = 0


    def place(self, label, top):
        """
        Emit a label that the stack reaches with its top entry at top
        """

        self.settle()
        self.target.instruction(OpCode.LABEL, label)
        self.top = top


    def join(self, text, home):
        """
        A new miss target, reached with the top entry at home
        """

        label = self.target.label(text)
        self.targets[label] = (home, home, label)
        return label


    def miss_at(self, miss, top=None):
        """
        The label a jump to the miss target takes from a stack whose
        top entry is at top, the current top by default: the target
        itself from its own top, else the fail entry for that top

        :raises ValueError: when top is below the target's
        """

        if top is None:
            top = self.top
        home = self.targets[miss][0]
        if top == home:
            return miss
        if top < home:
            raise ValueError(
                f"matcher miss from stack top {top} is below its"
                f" target's {home}")
        label = self.entries.get((miss, top))
        if label is None:
            label = self.target.label("match.fail")
            self.entries[(miss, top)] = label
        return label


    def goto(self, miss):
        """
        Leave for the miss target from the current top where the code
        falls through: the POPs down to its base and the jump to where
        it goes on, or the return of None for the standalone miss
        """

        _home, base, cont = self.targets[miss]
        self.op(OpCode.POP, self.top - base, effect=base - self.top)
        if cont is None:
            self.op(OpCode.CONST, None, effect=1)
            self.op(OpCode.RETURN, 1)
        else:
            self.op(OpCode.JUMP, cont)


    def land(self, label):
        """
        Place a miss target where the code before it never falls
        through, with the fail entry of its lowest pending top falling
        into it
        """

        home = self.targets[label][0]
        pending = [
            top for (miss, top), entry in self.entries.items()
            if miss == label and entry not in self.placed]
        self.settle()
        if pending:
            top = min(pending)
            entry = self.entries[(label, top)]
            self.placed.add(entry)
            self.target.instruction(OpCode.LABEL, entry)
            self.target.instruction(OpCode.POP, top - home)
        self.place(label, home)


    def pending(self):
        """
        The fail entries not yet emitted, as ((target, top), label)
        """

        return [
            item for item in self.entries.items()
            if item[1] not in self.placed]


    def flush(self, follow=None):
        """
        Emit the fail entries not yet emitted, each popping down to its
        target's base and going on where the target does. follow is
        the label the caller places next: the last entry going on there
        falls into it
        """

        self.settle()
        pending = self.pending()
        if follow is not None:
            pending.sort(
                key=lambda item: self.targets[item[0][0]][2] == follow)
        for n, ((miss, top), entry) in enumerate(pending, 1):
            _home, base, cont = self.targets[miss]
            self.placed.add(entry)
            self.target.instruction(OpCode.LABEL, entry)
            self.target.instruction(OpCode.POP, top - base)
            if cont is None:
                self.target.instruction(OpCode.CONST, None)
                self.target.instruction(OpCode.RETURN, 1)
            elif cont != follow or n < len(pending):
                self.target.instruction(OpCode.JUMP, cont)


    def fits(self, cur, bounds, miss):
        """
        The length precheck on the cursor at cur; none for bounds that
        every length meets
        """

        low, high = bounds
        if low == 0 and high is None:
            return
        self.op(
            OpCode.MATCH_FITS, self.miss_at(miss), self.top - cur, low,
            UNBOUNDED if high is None else high)


    def store(self, slot, cur=None):
        """
        Store the value on top of the stack into a slot: a root slot of
        a match arm by the store an assignment to its name emits, any
        other into its entry in the mark of its scope. cur is the
        cursor the value came from, None for a value tested in place
        """

        analysis = self.analysis
        scope = analysis.slot_scope[slot]
        index = analysis.slot_index[slot]
        names = analysis.scopes[scope].names
        if self.frame and scope == 0:
            self.op(OpCode.VAR_STORE, symbol(names[index]), effect=-1)
            return
        # depth 0 is the value itself, so it names no cursor
        self.op(
            OpCode.MATCH_STORE, self.top - self.marks[scope], index,
            0 if cur is None else self.top - cur, names, effect=-1)


    def value(self, sub, miss):
        """
        Match the value on top of the stack against sub on a cursor of
        its own, which replaces the value; on a hit the cursor is
        popped
        """

        self.op(OpCode.MATCH_SINGLE)
        inner = self.top
        self.emit(sub, inner, miss)
        self.fits(inner, (0, 0), miss)
        self.op(OpCode.POP, 1, effect=-1)


    def in_place(self, i):
        """
        Whether node i, one element wide, is tested on the value with
        no cursor: one element test, or a ONEOF whose branches are each
        tested in place, under CAPTUREs and one-child MATCHes only
        """

        node = self.nodes[i]
        kind = node[0]
        if kind in ELEMENT_KINDS:
            return True
        if kind == 'CAPTURE':
            return node[2] == -1 or self.in_place(node[2])
        if kind == 'ONEOF':
            return all(self.in_place(branch) for branch in node[1:])
        return kind == 'MATCH' and len(node) == 2 and self.in_place(node[1])


    def single(self, sub, miss):
        """
        Match the value on top of the stack against sub, a single
        value's pattern, the value gone on a hit: in place when sub is
        tested in place, else on a cursor of its own
        """

        if self.in_place(sub):
            self.test(sub, miss, False)
        else:
            self.value(sub, miss)


    def test(self, i, miss, keep, cur=None):
        """
        Match node i, tested in place, against the value on top of the
        stack. On a hit the value is left on top when keep, else gone;
        a miss jumps from the stack as the failing step leaves it. A
        test leaves its value on both exits; a FORM, APP or PREDICATE
        consumes it, so it tests a copy when the value is wanted after
        the hit or at the miss target. cur is the cursor the value came
        from, None for a single value
        """

        nodes = self.nodes
        node = nodes[i]
        kind = node[0]

        if kind == 'MATCH':
            self.test(node[1], miss, keep, cur)

        elif kind == 'CAPTURE':
            # tested first, so a miss stores nothing
            slot, sub = node[1], node[2]
            if sub != -1:
                self.test(sub, miss, True, cur)
            if keep:
                self.op(OpCode.DUP, 1, effect=1)
            self.store(slot, cur)

        elif kind == 'ONEOF':
            self.oneof(i, miss, keep, cur)

        elif kind == 'SAME' and self.analysis.same[i] is None:
            # no scope at its depth binds the name: always a miss
            self.op(OpCode.JUMP, self.miss_at(miss))
            if not keep:
                self.top -= 1

        elif kind in ('FORM', 'APP') or (
                kind == 'TEST' and node[1] == 'PREDICATE'):
            copy = keep or self.targets[miss][0] >= self.top
            if copy:
                self.op(OpCode.DUP, 1, effect=1)
            self.consume(i, miss)
            if copy and not keep:
                self.op(OpCode.POP, 1, effect=-1)

        else:
            if kind in ('CLASS', 'MAP'):
                self.gate(i, miss)
            elif kind == 'SAME':
                scope, index = self.analysis.same[i]
                self.op(
                    OpCode.MATCH_SAME, self.miss_at(miss),
                    self.top - self.marks[scope], index)
            elif node[1] == 'LITERAL':
                self.op(
                    OpCode.MATCH_LITERAL, self.miss_at(miss),
                    self.consts[node[2]])
            elif node[1] == 'CORE':
                self.op(OpCode.MATCH_CORE, self.miss_at(miss), int(node[2]))
            elif node[1] == 'TYPE':
                self.op(
                    OpCode.MATCH_TYPE_VAR, self.miss_at(miss),
                    self.externs[node[2]])
            # TEST ANY tests nothing
            if not keep:
                self.op(OpCode.POP, 1, effect=-1)


    def consume(self, i, miss):
        """
        Test the value on top of the stack against node i, a FORM, an
        APP or a PREDICATE, consuming the value on a hit and on a miss
        """

        analysis = self.analysis
        node = self.nodes[i]
        kind = node[0]

        if kind == 'TEST':
            self.op(OpCode.CALL_VAR, self.externs[node[2]], 1)
            self.op(
                OpCode.JUMP_FALSE, self.miss_at(miss, self.top - 1),
                effect=-1)

        elif kind == 'FORM':
            self.op(OpCode.MATCH_DESCEND, self.miss_at(miss, self.top - 1))
            inner = self.top
            self.fits(inner, analysis.form_fits[i], miss)
            self.emit(node[1], inner, miss)
            self.fits(inner, (0, 0), miss)
            self.op(OpCode.POP, 1, effect=-1)

        else:
            self.op(OpCode.CALL_VAR, self.externs[node[1]], 1)
            if node[2] == -1:
                self.op(OpCode.POP, 1, effect=-1)
            else:
                self.single(node[2], miss)


    def gate(self, i, miss):
        """
        A CLASS or MAP against the value on top of the stack, which
        stays: the gate, then each field or entry value pushed over the
        object and matched as a single value
        """

        nodes = self.nodes
        node = nodes[i]

        if node[0] == 'CLASS':
            cls = self.externs[node[1]]
            positional = sum(
                1 for field in node[2:] if nodes[field][0] == 'FIELD_POS')
            if positional:
                # the __match_args__ count is checked before any field
                # is read
                self.op(
                    OpCode.MATCH_CLASS_VAR, self.miss_at(miss), cls,
                    positional)
            else:
                self.op(OpCode.MATCH_TYPE_VAR, self.miss_at(miss), cls)
            position = 0
            for field in node[2:]:
                field = nodes[field]
                if field[0] == 'FIELD_POS':
                    self.op(
                        OpCode.MATCH_FIELD_POS_VAR, self.miss_at(miss), cls,
                        position, effect=1)
                    position += 1
                else:
                    self.op(
                        OpCode.MATCH_FIELD_KW, self.miss_at(miss), field[2],
                        effect=1)
                self.single(field[1], miss)

        else:
            self.op(OpCode.MATCH_MAPPING, self.miss_at(miss))
            for entry in node[1:]:
                entry = nodes[entry]
                if entry[0] == 'MAP_ENTRY_CONST':
                    self.op(
                        OpCode.MATCH_KEY, self.miss_at(miss),
                        self.consts[entry[1]], effect=1)
                else:
                    self.op(
                        OpCode.MATCH_KEY_VAR, self.miss_at(miss),
                        self.externs[entry[1]], effect=1)
                if entry[2] == -1:
                    self.op(OpCode.POP, 1, effect=-1)
                else:
                    self.single(entry[2], miss)


    def oneof(self, i, miss, keep, cur):
        """
        A ONEOF tested in place against the value on top of the stack:
        each branch tests the value in turn, and the first that matches
        is taken. A branch that misses leaves the value for the next,
        and empties the slots it could have bound when a miss in it can
        follow a store; the last branch misses to miss
        """

        node = self.nodes[i]
        scope = self.analysis.node_scope[i]
        value = self.top
        done = self.target.label("match.oneof")
        branches = node[1:]
        for branch in branches[:-1]:
            nxt = self.join("match.oneof", value)
            self.test(branch, nxt, keep, cur)
            self.op(OpCode.JUMP, done)
            self.land(nxt)
            if self.stores_ahead(branch):
                self.clear(scope, branch)
        self.test(branches[-1], miss, keep, cur)
        self.place(done, value if keep else value - 1)


    def stores_ahead(self, i):
        """
        Whether a capture under node i, tested in place, can be stored
        before a later step of i misses: one under a FORM, APP, CLASS or
        MAP can; a CAPTURE over a test stores after the test
        """

        node = self.nodes[i]
        kind = node[0]
        if kind == 'MATCH':
            return self.stores_ahead(node[1])
        if kind == 'CAPTURE':
            return node[2] != -1 and self.stores_ahead(node[2])
        if kind == 'ONEOF':
            return any(self.stores_ahead(branch) for branch in node[1:])
        return any(self.binds(child) for child in node_children(node))


    def binds(self, i):
        """
        Whether node i's subtree stores a capture: a CAPTURE, or an
        uncaptured REPEAT's columns
        """

        node = self.nodes[i]
        if node[0] == 'CAPTURE' or i in self.analysis.columns:
            return True
        return any(self.binds(child) for child in node_children(node))


    def stores(self, i):
        """
        How many CAPTUREs on one path through node i, tested in place,
        store its element into a mark, each setting the binding of a
        spine element's Datum: two of them would share one Datum, so
        on a cursor the outer captures each take the element from a
        mark of their own instead, as MATCH_AT gives a fresh Datum
        """

        node = self.nodes[i]
        kind = node[0]
        if kind == 'MATCH':
            return self.stores(node[1])
        if kind == 'ONEOF':
            return max(self.stores(branch) for branch in node[1:])
        if kind != 'CAPTURE':
            return 0
        scope = self.analysis.slot_scope[node[1]]
        own = 0 if self.frame and scope == 0 else 1
        return own + (0 if node[2] == -1 else self.stores(node[2]))


    def first_fits(self, i):
        """
        The precheck of the first branch of the ONEOF at the head of
        node i, under CAPTUREs and one-child MATCHes, None when there
        is none: one element wide, every branch has the same, and it
        runs once ahead of the element's pull
        """

        nodes = self.nodes
        while True:
            node = nodes[i]
            if node[0] == 'MATCH':
                i = node[1]
            elif node[0] == 'CAPTURE' and node[2] != -1:
                i = node[2]
            elif node[0] == 'ONEOF':
                return self.analysis.fits.get(node[1])
            else:
                return None


    def standalone(self):
        """
        The walk as a function body: the subject formal walked as a
        sequence, the root scope's captures returned on a match, None
        on a miss
        """

        analysis = self.analysis
        names = analysis.scopes[0].names
        miss = self.target.label("match.miss")
        self.targets[miss] = (-1, -1, None)

        self.op(OpCode.VAR, self.target.subject, effect=1)
        self.op(OpCode.MATCH_DESCEND, self.miss_at(miss, self.top - 1))
        cur = self.top
        if analysis.root_fits is not None:
            self.fits(cur, analysis.root_fits, miss)
        self.op(OpCode.MATCH_SCOPE, self.top - cur, len(names), effect=1)
        self.marks[0] = self.top
        self.emit(self.entry, cur, miss)
        self.fits(cur, (0, 0), miss)
        self.op(
            OpCode.MATCH_CAPTURES, self.top - self.marks[0], self.top - cur,
            names, effect=1)
        self.op(OpCode.RETURN, 1)

        self.land(miss)
        self.op(OpCode.CONST, None, effect=1)
        self.op(OpCode.RETURN, 1)
        self.flush()


    def arm(self, miss, guarded=False):
        """
        The walk of a match arm over the subject on top of the stack.
        Each root capture is stored into its variable, by the store an
        assignment to its name emits, as soon as its value is tested;
        a miss leaves what was stored. On a hit the walk falls through
        with the subject consumed, or for a guarded arm kept; on a miss
        it jumps to miss with the subject kept

        :return: a function the caller invokes where code after the
            arm's hit path is unreachable and just ahead of miss,
            emitting the arm's fail entries, or None when the arm has
            none
        """

        self.top = 0
        self.targets[miss] = (0, 0, miss)
        if self.in_place(self.entry):
            self.test(self.entry, miss, guarded)
        else:
            self.op(OpCode.DUP, 1, effect=1)
            self.op(OpCode.MATCH_SINGLE)
            cur = self.top
            self.emit(self.entry, cur, miss)
            self.fits(cur, (0, 0), miss)
            self.op(OpCode.POP, 1, effect=-1)
            if not guarded:
                self.op(OpCode.POP, 1, effect=-1)
        self.settle()

        if not self.pending():
            return None
        return partial(self.flush, miss)


    def emit(self, i, cur, miss):
        """
        Emit node i against the cursor at entry cur. The stack is as
        the node found it where its code falls through on a hit, but
        for a captured REPEAT or REST, which leaves its value on top; a
        miss jumps to miss from the stack as the failing step leaves
        it, through the fail entry for that top
        """

        analysis = self.analysis
        nodes = self.nodes
        node = nodes[i]
        kind = node[0]
        label = self.target.label
        # the position of the first entry the node pushes
        start = self.top + 1

        if i in analysis.fits:
            self.fits(cur, analysis.fits[i], miss)

        if kind == 'SAME' and analysis.same[i] is None:
            # no scope at its depth binds the name: always a miss
            self.op(OpCode.JUMP, self.miss_at(miss))

        elif self.in_place(i) and self.stores(i) < 2:
            # the element pulled once, then tested on the stack
            bounds = self.first_fits(i)
            if bounds is not None:
                self.fits(cur, bounds, miss)
            self.op(
                OpCode.MATCH_NEXT, self.miss_at(miss), self.top - cur,
                effect=1)
            self.test(i, miss, False, cur)

        elif kind == 'MATCH' or (kind == 'GROUP'
                                 and i not in analysis.group_scope):
            # a GROUP that is a REPEAT's body runs in the pass's scope
            for child in node[1:]:
                self.emit(child, cur, miss)

        elif kind == 'GROUP':
            scope = analysis.group_scope[i]
            names = analysis.scopes[scope].names
            self.op(OpCode.MATCH_SCOPE, self.top - cur, len(names), effect=1)
            self.marks[scope] = start
            for child in node[1:]:
                self.emit(child, cur, miss)
            if analysis.scopes[scope].kind == 'group':
                self.op(
                    OpCode.MATCH_CAPTURES, self.top - start, self.top - cur,
                    names, effect=1)
                self.store(nodes[self.captured[i]][1], cur)
            self.op(OpCode.POP, 1, effect=-1)
            del self.marks[scope]

        elif kind == 'CAPTURE':
            slot, sub = node[1], node[2]
            if nodes[sub][0] in ('REPEAT', 'REST'):
                self.emit(sub, cur, miss)
                self.store(slot, cur)
            elif nodes[sub][0] == 'GROUP':
                self.emit(sub, cur, miss)
            else:
                self.op(OpCode.MATCH_SCOPE, self.top - cur, 0, effect=1)
                self.emit(sub, cur, miss)
                if analysis.widths[sub] == (1, 1):
                    self.op(OpCode.MATCH_AT, self.top - start, effect=1)
                    self.store(slot, cur)
                else:
                    # an empty span leaves the slot unbound
                    skip = label("match.empty")
                    self.op(
                        OpCode.MATCH_SPAN, skip, self.top - start,
                        self.top - cur, effect=1)
                    self.store(slot, cur)
                    self.place(skip, start)
                self.op(OpCode.POP, 1, effect=-1)

        elif kind == 'ONEOF':
            # a failed branch empties the slots it could have bound,
            # but for an arm's root scope, whose variables keep what
            # the branch stored
            scope = analysis.node_scope[i]
            done = label("match.oneof")
            self.op(OpCode.MATCH_SCOPE, self.top - cur, 0, effect=1)
            for branch in node[1:]:
                nxt = self.join("match.oneof", start)
                self.emit(branch, cur, nxt)
                self.op(OpCode.POP, 1, effect=-1)
                self.op(OpCode.JUMP, done)
                self.land(nxt)
                self.op(OpCode.MATCH_RESET, self.top - cur, self.top - start)
                self.clear(scope, branch)
            self.goto(miss)
            self.place(done, start - 1)

        elif kind == 'OPTIONAL':
            back = self.join("match.opt", start)
            done = label("match.opt")
            self.op(OpCode.MATCH_SCOPE, self.top - cur, 0, effect=1)
            self.emit(node[1], cur, back)
            self.op(OpCode.POP, 1, effect=-1)
            self.op(OpCode.JUMP, done)
            self.land(back)
            self.op(OpCode.MATCH_RESET, self.top - cur, self.top - start)
            self.clear(analysis.node_scope[i], node[1])
            self.op(OpCode.POP, 1, effect=-1)
            self.place(done, start - 1)

        elif kind == 'NOT':
            # the sub-pattern moves only the copy, which both exits
            # pop; a miss of the sub-pattern from above the copy pops
            # it with the rest
            ok = label("match.not")
            past = label("match.not")
            self.targets[ok] = (start, start - 1, past)
            self.op(OpCode.MATCH_UNBOUND, self.top - cur, effect=1)
            self.emit(node[1], start, ok)
            self.goto(miss)
            self.land(ok)
            self.op(OpCode.POP, 1, effect=-1)
            self.place(past, start - 1)

        elif kind == 'REPEAT':
            self.emit_repeat(i, cur, miss)

        elif kind == 'REST':
            self.op(
                OpCode.MATCH_TAIL, self.miss_at(miss), self.top - cur,
                analysis.suffix[i][0], node[1], effect=1)
            if i not in self.captured:
                self.op(OpCode.POP, 1, effect=-1)

        else:
            raise ValueError(
                f"matcher node kind {kind!r} at node {i} is not on a"
                f" cursor")


    def clear(self, scope, branch):
        """
        Empty the slots a failed alternative could have bound in the
        mark of scope; an arm's root scope has no mark, and its
        variables keep what the alternative stored
        """

        cleared = self.analysis.branch_slots.get(branch)
        if cleared and not (self.frame and scope == 0):
            self.op(
                OpCode.MATCH_CLEAR, self.top - self.marks[scope], cleared)


    def emit_repeat(self, i, cur, miss):
        """
        A REPEAT on a cursor bounded w before cur's end, one pass per
        iteration in a scope of its own, each pass's value appended to
        the items list; cur is synced to the bound cursor at the exit.
        A captured REPEAT leaves the items list on top for its CAPTURE;
        an uncaptured one stores the column of each of its pass slots
        into the enclosing scope
        """

        analysis = self.analysis
        node = self.nodes[i]
        label = self.target.label
        items = self.top + 1
        bound = items + 1
        mark = items + 2
        scope = analysis.pass_scope[i]
        names = analysis.scopes[scope].names
        captured = i in self.captured
        head = label("match.repeat")
        pass_miss = self.join("match.repeat", mark)
        done = label("match.repeat")

        self.op(OpCode.BUILD_LIST, 0, effect=1)
        self.op(
            OpCode.MATCH_BOUND, self.miss_at(miss), self.top - cur,
            analysis.suffix[i][0], effect=1)

        self.place(head, bound)
        self.op(OpCode.MATCH_FITS, done, self.top - bound, 1, UNBOUNDED)
        self.op(OpCode.MATCH_SCOPE, self.top - bound, len(names), effect=1)
        self.marks[scope] = mark
        self.emit(node[1], bound, pass_miss)
        if captured:
            # a GROUP body's pass is its captures, as MATCH_CAPTURES
            # gives them, when it binds a slot
            group = int(self.nodes[node[1]][0] == 'GROUP')
            self.op(
                OpCode.MATCH_PASS, self.top - mark, self.top - bound, names,
                group, effect=1)
        elif names:
            self.op(
                OpCode.MATCH_CAPTURES, self.top - mark, self.top - bound,
                names, effect=1)
        else:
            self.op(OpCode.CONST, None, effect=1)
        self.op(OpCode.MATCH_APPEND, self.top - items, effect=-1)
        self.op(OpCode.POP, 1, effect=-1)
        self.op(OpCode.JUMP, head)

        self.land(pass_miss)
        self.op(OpCode.MATCH_RESET, self.top - bound, self.top - mark)
        self.op(OpCode.POP, 1, effect=-1)
        del self.marks[scope]

        self.place(done, bound)
        self.op(OpCode.MATCH_SYNC, self.top - cur, self.top - bound)
        self.op(OpCode.POP, 1, effect=-1)
        if node[2]:
            self.op(
                OpCode.MATCH_MIN, self.miss_at(miss), self.top - items,
                node[2])
        if not captured:
            if names and self.frame and analysis.node_scope[i] == 0:
                self.store_columns(i, cur, items)
            elif names:
                outer = self.marks[analysis.node_scope[i]]
                self.op(
                    OpCode.MATCH_COLUMNS, self.top - items,
                    self.top - outer, names, analysis.columns[i])
            self.op(OpCode.POP, 1, effect=-1)


    def store_columns(self, i, cur, items):
        """
        Store the columns of an uncaptured REPEAT into the variables of
        a match arm's root scope: rolled up in a mark of their own,
        then each loaded and stored by the store an assignment to its
        name emits
        """

        names = self.analysis.scopes[self.analysis.pass_scope[i]].names
        self.op(OpCode.MATCH_SCOPE, self.top - cur, len(names), effect=1)
        self.op(
            OpCode.MATCH_COLUMNS, self.top - items, 0, names,
            tuple(range(len(names))))
        for index, name in enumerate(names):
            skip = self.target.label("match.unbound")
            self.op(OpCode.MATCH_LOAD, skip, 0, index, effect=1)
            self.op(OpCode.VAR_STORE, symbol(name), effect=-1)
            self.place(skip, self.top)
        self.op(OpCode.POP, 1, effect=-1)


# The end.
