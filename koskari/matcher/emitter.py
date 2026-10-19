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
koskari.matcher.emitter

The emitter that lowers a pattern's tables, with their analysis, to the
MATCH_* instructions, the fragment target it emits into, and the entry
points for a match arm and a whole matcher body.

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
list by its depth under the top as the instruction finds the stack

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from functools import partial

from koskari.lib import OpCode, symbol

from .analysis import ELEMENT_KINDS, UNBOUNDED, node_children


__all__ = (
    'lower_arm',
)


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

    def __init__(self, target, tables, analysis, atoms, arm=False):
        """
        :param target: a FragmentTarget
        :param tables: (slots, externs, consts, nodes, entry)
        :param analysis: the Analysis analyse made of the tables, with
            the same arm
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
        self.analysis = analysis
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
        # an arm's (gate miss, label after the gate) until its first
        # type test, the gate, takes them
        self.gate_labels = None
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
        itself from its own top or for the standalone miss, whose
        return leaves the stack to the frame, else the fail entry for
        that top

        :raises ValueError: when top is below the target's
        """

        if top is None:
            top = self.top
        home, _base, cont = self.targets[miss]
        if top == home or cont is None:
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
        it goes on, or for the standalone miss the return of None in
        place, the stack left to the frame
        """

        _home, base, cont = self.targets[miss]
        if cont is None:
            self.op(OpCode.RETURN, 0)
            return
        self.op(OpCode.POP, self.top - base, effect=base - self.top)
        self.op(OpCode.JUMP, cont)


    def always_miss(self, miss):
        """
        Leave for the miss target from a test that cannot pass: the
        jump to its entry for the current top, or for the standalone
        miss the return of None in place
        """

        if self.targets[miss][2] is None:
            self.op(OpCode.RETURN, 0)
        else:
            self.op(OpCode.JUMP, self.miss_at(miss))


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
            if cont != follow or n < len(pending):
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
            self.always_miss(miss)
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
                self.type_test(OpCode.MATCH_CORE, miss, int(node[2]))
            elif node[1] == 'TYPE':
                self.type_test(
                    OpCode.MATCH_TYPE_VAR, miss, self.externs[node[2]])
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
            # a fixed width was checked exactly before the elements were
            # tested, and the body pulls exactly that many
            if not analysis.fixed[node[1]]:
                self.fits(inner, (0, 0), miss)
            self.op(OpCode.POP, 1, effect=-1)

        else:
            self.op(OpCode.CALL_VAR, self.externs[node[1]], 1)
            if node[2] == -1:
                self.op(OpCode.POP, 1, effect=-1)
            else:
                self.single(node[2], miss)


    def type_test(self, opcode, miss, *fields):
        """
        A CORE, TYPE or CLASS test on the value on top of the stack. The
        first one of an arm given gate labels is its gate: its miss goes
        to the gate miss, and the label after the gate is placed past it
        """

        gate, self.gate_labels = self.gate_labels, None
        if gate is None:
            self.op(opcode, self.miss_at(miss), *fields)
            return
        gate_miss, after_gate = gate
        self.op(opcode, gate_miss, *fields)
        if after_gate is not None:
            self.place(after_gate, self.top)


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
                self.type_test(
                    OpCode.MATCH_CLASS_VAR, miss, cls, positional)
            else:
                self.type_test(OpCode.MATCH_TYPE_VAR, miss, cls)
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
        # as a FORM's: a fixed width was checked exactly at the root
        if not analysis.fixed[self.entry]:
            self.fits(cur, (0, 0), miss)
        self.op(
            OpCode.MATCH_CAPTURES, self.top - self.marks[0], self.top - cur,
            names, effect=1)
        self.op(OpCode.RETURN, 1)

        self.place(miss, -1)
        self.op(OpCode.RETURN, 0)
        self.flush()


    def arm(
            self,
            miss,
            guarded=False,
            gate_miss=None,
            after_gate=None,
            follow=None):
        """
        The walk of a match arm over the subject on top of the stack.
        Each root capture is stored into its variable, by the store an
        assignment to its name emits, as soon as its value is tested;
        a miss leaves what was stored. On a hit the walk falls through
        with the subject consumed, or for a guarded arm kept; on a miss
        it jumps to miss with the subject kept. An arm with a gate
        (arm_gate) sends the gate's own miss to gate_miss and places
        after_gate just past it, each when given

        :param follow: the label the caller places right after the fail
            entries, miss by default
        :return: a function the caller invokes where code after the
            arm's hit path is unreachable and just ahead of follow,
            emitting the arm's fail entries, or None when the arm has
            none
        """

        self.top = 0
        self.targets[miss] = (0, 0, miss)
        if gate_miss is not None or after_gate is not None:
            self.gate_labels = (gate_miss or miss, after_gate)
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
        if self.gate_labels is not None:
            raise ValueError(
                "match arm gate labels given to an arm with no gate")

        if not self.pending():
            return None
        return partial(self.flush, miss if follow is None else follow)


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
            self.always_miss(miss)

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


class FragmentTarget:
    """
    The inline target: the instructions go into a fragment of the
    standard compiler, which names the slots and lowers them to fast
    indices itself. A whole matcher lowered into it reads the subject
    as the gensym the fragment declares; an inline arm has the subject
    on the stack and names none
    """

    def __init__(self, compiler, fragment, subject=None):
        self.compiler = compiler
        self.fragment = fragment
        self.subject = subject


    def instruction(self, op, arg):
        self.fragment.instruction(op, arg)


    def label(self, text):
        return self.compiler.gen_label(text)


def lower_arm(
        compiler,
        fragment,
        tables,
        analysis,
        atoms,
        miss,
        guarded=False,
        gate_miss=None,
        after_gate=None,
        follow=None):
    """
    Emit the walk of a match arm over the subject on top of the stack.
    Each root capture is stored into its variable as soon as its value
    is tested, and a miss leaves what was stored. On a hit the walk
    falls through with the subject consumed, or for a guarded arm
    kept; on a miss it jumps to miss with the subject kept. Externs
    are read from the atoms the pattern wrote.

    :param compiler: the compiler, for labels
    :param fragment: the fragment to emit into
    :param tables: the slots, externs, consts, nodes and entry
        TableBuilder lowered the pattern to
    :param analysis: the Analysis analyse made of the tables as an arm
    :param atoms: the extern names mapped to the atoms the pattern
        wrote them as
    :param miss: the arm's miss label
    :param guarded: whether a guard follows, which needs the subject
    :param gate_miss: where the arm's gate (arm_gate) jumps on its own
        miss, miss by default
    :param after_gate: a label to place just past the gate, or None
    :param follow: the label the caller places right after the fail
        entries, miss by default
    :return: a function the caller invokes where code after the arm's
        hit path is unreachable and just ahead of follow, emitting the
        arm's fail entries, or None when the arm has none
    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    emitter = Emitter(
        FragmentTarget(compiler, fragment), tables, analysis, atoms,
        arm=True)
    return emitter.arm(miss, guarded, gate_miss, after_gate, follow)


def lower_matcher(target, tables, analysis, atoms):
    """
    Emit a whole matcher as a function body into a fragment target:
    the subject formal, the target's subject gensym, declared by the
    caller as the body's first local, walked as a sequence, the
    captures returned on a match, None on a miss

    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    Emitter(target, tables, analysis, atoms).standalone()


# The end.
