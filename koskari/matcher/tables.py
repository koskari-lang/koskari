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
koskari.matcher.tables

The lowering of a pattern tree to the node tables its matcher is built
from: the slots, externs, consts and nodes, and the entry node

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


__all__ = (
    'TableBuilder',
)


class TableBuilder:
    """
    Baseline compiler for one lowering pass. Holds the four
    insertion-ordered tables, the slots, externs, consts, and nodes,
    that the lowering takes, plus capture-scope bookkeeping
    """

    def __init__(self):
        """
        Construct an empty baseline match compiler
        """

        self.slot_names = []
        self.formal_slot_names = []
        self.extern_indices = {}
        self.consts = []
        self.nodes = []
        self.capture_scope_stack = [{}]
        self._formal_scope_stack = [True]
        # per capture scope, the slot index of each name interned in
        # it, whether the scope is a frame of its own (the root, a
        # GROUP or a REPEAT) or shares the enclosing frame (a ONEOF
        # branch), and for a ONEOF branch the slot index of each name
        # its ONEOF's alternatives interned; SAME resolves its name
        # through these
        self._slot_scopes = [({}, True, None, False)]
        self.in_class_fields = False
        # per claimed slot name, the number of ellipses a template must
        # place it under: the repeats around the claim, plus one for a
        # capture that is itself a sequence
        self.ellipsis_depths = {}


    def begin_nested_claims(
            self,
            inherit_formals=True,
            alternatives=None,
            boundary=False):
        """
        Begin a nested region where duplicate capture slot names are isolated
        (for example ONEOF branches or GROUP children). When
        ``inherit_formals`` is false, slots interned inside the region are
        omitted from :data:`formal_slot_names`: a GROUP or REPEAT row's
        slots are fields of the row, and a ONEOF branch's are formals

        :param alternatives: for a ONEOF branch, a dict the ONEOF
            shares across its branches; a name the branches bind has
            one slot index across them
        :param boundary: for a REPEAT body: a name interned inside it
            takes no index from a ONEOF alternative outside it
        """

        self.capture_scope_stack.append({})
        self._slot_scopes.append(
            ({}, not inherit_formals, alternatives, boundary))
        if inherit_formals:
            self._formal_scope_stack.append(self._formal_scope_stack[-1])
        else:
            self._formal_scope_stack.append(False)


    def end_nested_claims(self):
        """
        End the innermost nested duplicate-slot region
        """

        if len(self.capture_scope_stack) <= 1:
            raise RuntimeError(
                "end_nested_claims underflow (unbalanced nested scope)")
        self.capture_scope_stack.pop()
        slots, frame, _alternatives, _boundary = self._slot_scopes.pop()
        self._formal_scope_stack.pop()
        if not frame:
            # a ONEOF branch's names stay visible to the nodes after
            # the ONEOF
            for name, idx in slots.items():
                self._slot_scopes[-1][0].setdefault(name, idx)


    def intern_slot(self, name):
        """
        Append a capture slot name and return its index. Duplicate names
        are allowed (distinct indices), but for the alternatives of one
        ONEOF, which share one index for a name; use claim_slot_name to
        forbid duplicates within a capture scope
        """

        # the ONEOF branches between here and the nearest frame: an
        # alternative that interned the name gave its index
        branches = []
        for _slots, frame, alternatives, boundary in reversed(
                self._slot_scopes):
            if frame:
                break
            branches.append(alternatives)
            if boundary:
                break
        for alternatives in branches:
            if alternatives is not None and name in alternatives:
                idx = alternatives[name]
                self._slot_scopes[-1][0][name] = idx
                return idx
        for alternatives in branches:
            if alternatives is not None:
                alternatives[name] = len(self.slot_names)

        idx = len(self.slot_names)
        self.slot_names.append(name)
        self._slot_scopes[-1][0][name] = idx
        if self._formal_scope_stack[-1]:
            self.formal_slot_names.append(name)
        return idx


    def resolve_slot(self, name, depth):
        """
        The slot a SAME names: the scopes from the innermost down to
        and including the nearest frame are depth 0, the next frame
        depth 1, and the first scope at the requested depth holding
        the name gives the slot. A SAME may read a slot before its
        binding, or one no scope at its depth binds: the name gets a
        slot of its own, and the analysis finds the scope that binds
        the name, or none, a SAME that always misses
        """

        level = 0
        for slots, frame, _alternatives, _boundary in reversed(
                self._slot_scopes):
            if level == depth and name in slots:
                return slots[name]
            if frame:
                level += 1
                if level > depth:
                    break
        idx = len(self.slot_names)
        self.slot_names.append(name)
        return idx


    def intern_extern(self, name):
        """
        Return the extern index for the named extern, allocating one when
        first seen
        """

        if name in self.extern_indices:
            return self.extern_indices[name]

        idx = len(self.extern_indices)
        self.extern_indices[name] = idx
        return idx


    def intern_const(self, value):
        """
        Return the const index for the given value, allocating one when
        no equal-and-same-type entry already exists. Type-aware to
        preserve distinctions like 0 vs False
        """

        for i, existing in enumerate(self.consts):
            if type(existing) is type(value) and existing == value:
                return i

        idx = len(self.consts)
        self.consts.append(value)
        return idx


    def append_node(self, node_tuple):
        """
        Append a node tuple to the nodes table and return its index
        """

        idx = len(self.nodes)
        self.nodes.append(node_tuple)
        return idx


    def claim_slot_name(self, name, signature, sequence=False):
        """
        Reject duplicate slot bindings within the current capture scope.
        Repeat a pattern variable with :class:`SAME` instead of a second
        CAPTURE. ONEOF branches use a fresh scope per branch so alternate
        arms may reuse slot names. The claim's ellipsis depth is recorded
        in :data:`ellipsis_depths`

        :param name: capture slot name
        :param signature: hashable tuple describing this claim
        :param sequence: the slot captures a sequence of matches, a
            repeat's iterations or a rest's elements
        """

        top = self.capture_scope_stack[-1]
        if name in top:
            prev = top[name]
            if (isinstance(prev, tuple) and isinstance(signature, tuple)
                    and len(prev) >= 1 and len(signature) >= 1
                    and prev[0] == "capture" and signature[0] == "capture"):
                raise ValueError(
                    f"duplicate slot name {name!r} in matcher pattern "
                    "(differing matcher type); use SAME for repeats")
            raise ValueError(
                f"duplicate slot name {name!r} in matcher pattern; use SAME "
                "for repeats")
        top[name] = signature

        repeats = sum(1 for scope in self._slot_scopes if scope[3])
        self.ellipsis_depths[name] = repeats + (1 if sequence else 0)


    def lower(self, root):
        """
        Lower root into this compiler and return its tables, the
        lowering's input

        :param root: pattern to lower
        :return: (slots, externs, consts, nodes, entry)
        """

        entry = root.lower(self)
        return (
            tuple(self.slot_names),
            tuple(self.extern_indices),
            tuple(self.consts),
            tuple(self.nodes),
            entry,
        )


# The end.
