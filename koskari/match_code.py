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
koskari.match_code

A frozen matcher lowered to the MATCH_* instructions: each node pulled
from a cursor slot or peeked on the stack, with a miss edge to a label.
Two targets take the same instructions. The inline target emits into
a fragment of the standard compiler for a match clause, the captures
stored into the clause's declared names. The standalone target builds
a Function of its own from the instructions, with no compiler in
reach, whose result is the captures, or the index and captures of the
first clause that matches when a dispatcher is built over several.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from .exceptions import SpecialError
from .lib import (
    Code, Compare, CoreType, FormalsSpec, Function, NamespaceSpec, OpCode,
    OpStyle, gensym, symbol, values)


__all__ = (
    'FragmentTarget',
    'FunctionTarget',
    'compile_dispatch_function',
    'compile_match_function',
    'lower_clause',
    'lower_matcher',
    'validate_frozen',
)


UNBOUNDED = 0xFF
LIMIT = 0xFF

SUBJECT = "subject"


# === validation of a frozen matcher ===


NODE_ARITY = {
    'TEST': (3, 3),
    'CAPTURE': (3, 3),
    'FORM': (2, 2),
    'APP': (3, 3),
    'MAP_ENTRY_CONST': (3, 3),
    'MAP_ENTRY_EXTERN': (3, 3),
    'REPEAT': (2, 3),
    'OPTIONAL': (2, 2),
    'REST': (2, 2),
    'NOT': (2, 2),
    'SAME': (3, 3),
    'CLASS': (2, None),
    'FIELD_POS': (2, 2),
    'FIELD_KW': (3, 3),
    'MATCH': (1, None),
    'GROUP': (1, None),
    'ONEOF': (1, None),
    'MAP': (1, None),
}

CONTAINERS = ('MATCH', 'GROUP', 'ONEOF', 'MAP', 'CLASS')


def validate_frozen(frozen):
    """
    Check a frozen matcher tuple before anything is emitted from it:
    the tuple shape, every node's arity and references, the
    chains, the subnodes a kind requires, a REPEAT that consumes, no
    capture under NOT, at most one REST per branch with a fixed suffix
    after it, and the CLASS and FIELD structure. Iterative throughout,
    so a pattern nested past any limit is checked, not overflowed.

    :raises TypeError: when the tuple or a node is not the right type
    :raises ValueError: when the matcher is malformed
    """

    if not isinstance(frozen, tuple):
        raise TypeError("frozen matcher must be a tuple")
    if len(frozen) != 6:
        raise ValueError(f"frozen matcher must be 6-tuple, got {len(frozen)}")
    tag, slots, externs, consts, nodes, entry = frozen
    if tag != "MATCHER":
        raise ValueError("frozen matcher must start with 'MATCHER' tag")
    for part in (slots, externs, consts, nodes):
        if not isinstance(part, tuple):
            raise TypeError("slots/externs/consts/nodes must be tuples")
    if not isinstance(entry, int):
        raise TypeError("entry must be int")
    total = len(nodes)
    if total and not (0 <= entry < total):
        raise ValueError(f"entry index out of range: {entry}")

    def ref(value, size, what, i):
        if not isinstance(value, int):
            raise TypeError(f"{what} must be an int, got {type(value).__name__}")
        if not (0 <= value < size):
            raise ValueError(f"{what} out of range (size={size}) at node {i}")

    def subnode(value, i, required):
        if value == -1:
            if required:
                raise ValueError(f"{nodes[i][0]} at node {i} has no subnode")
            return
        ref(value, i, "subnode", i)

    # one pass over the nodes: arity, references, subnodes; a child is
    # always a lower index, so a cycle through the nodes is impossible
    claimed = {}
    for i, node in enumerate(nodes):
        if not isinstance(node, tuple) or not node:
            raise ValueError("frozen node must be a non-empty tuple")
        kind = node[0]
        if not isinstance(kind, str):
            raise TypeError("node kind tag must be str")
        arity = NODE_ARITY.get(kind)
        if arity is None:
            raise ValueError(f"unknown frozen node tag: {kind!r}")
        lo, hi = arity
        if len(node) < lo or (hi is not None and len(node) > hi):
            raise ValueError(
                f"{kind} node tuple has {len(node)} entries at node {i}")
        if kind == 'TEST':
            test = node[1]
            if test == 'LITERAL':
                ref(node[2], len(consts), "TEST_LITERAL ref_index", i)
            elif test in ('TYPE', 'PREDICATE'):
                ref(node[2], len(externs), "TEST ref_index", i)
            elif test == 'CORE':
                ref(node[2], len(CoreType), "TEST_CORE ordinal", i)
            elif test != 'ANY':
                raise ValueError(f"unknown test_kind: {test!r}")
        elif kind == 'CAPTURE':
            ref(node[1], len(slots), "CAPTURE slot", i)
            subnode(node[2], i, False)
        elif kind == 'SAME':
            ref(node[1], len(slots), "SAME slot", i)
            if not isinstance(node[2], int) or node[2] < 0:
                raise ValueError(f"SAME depth out of range at node {i}")
        elif kind in ('FORM', 'OPTIONAL', 'NOT'):
            subnode(node[1], i, True)
        elif kind == 'REPEAT':
            subnode(node[1], i, True)
            if len(node) == 3 and (not isinstance(node[2], int) or node[2] < 0):
                raise ValueError(f"REPEAT min_count out of range at node {i}")
        elif kind == 'REST':
            if not isinstance(node[1], int) or node[1] < 0:
                raise ValueError(f"REST min_tail out of range at node {i}")
        elif kind == 'APP':
            ref(node[1], len(externs), "APP fn_extern", i)
            subnode(node[2], i, False)
        elif kind == 'MAP_ENTRY_CONST':
            ref(node[1], len(consts), "MAP_ENTRY const key_ref", i)
            subnode(node[2], i, False)
        elif kind == 'MAP_ENTRY_EXTERN':
            ref(node[1], len(externs), "MAP_ENTRY extern key_ref", i)
            subnode(node[2], i, False)
        elif kind == 'FIELD_POS':
            subnode(node[1], i, True)
        elif kind == 'FIELD_KW':
            subnode(node[1], i, True)
            if not isinstance(node[2], str):
                raise TypeError(
                    f"FIELD_KW attr name must be str (got"
                    f" {type(node[2]).__name__})")
        elif kind == 'CLASS':
            ref(node[1], len(externs), "CLASS type_extern", i)
        if kind in CONTAINERS:
            children = node[2:] if kind == 'CLASS' else node[1:]
            seen = set()
            for child in children:
                ref(child, i, f"{kind} child", i)
                if child in seen:
                    raise ValueError(
                        f"{kind} at node {i} names child {child} twice")
                seen.add(child)
                owner = claimed.get(child)
                if owner is not None:
                    raise ValueError(
                        f"node {child} already participates in the chain"
                        f" of node {owner}; node {i} cannot claim it")
                claimed[child] = i
                if kind == 'CLASS' and nodes[child][0] not in (
                        'FIELD_POS', 'FIELD_KW'):
                    raise ValueError(
                        f"CLASS at node {i} references non-FIELD node"
                        f" {child}")
                if kind == 'MAP' and not nodes[child][0].startswith(
                        'MAP_ENTRY'):
                    raise ValueError(
                        f"MAP at node {i} references non-entry node {child}")

    # widths, bottom up: every child index is lower than its parent
    widths = []
    for i, node in enumerate(nodes):
        kind = node[0]
        if kind == 'CAPTURE':
            w = (1, 1) if node[2] == -1 else widths[node[2]]
        elif kind in ('TEST', 'FORM', 'APP', 'MAP', 'CLASS', 'SAME'):
            w = (1, 1)
        elif kind == 'NOT':
            w = (0, 0)
        elif kind == 'REST':
            w = (node[1], None)
        elif kind in ('MATCH', 'GROUP'):
            mn = 0
            mx = 0
            for child in node[1:]:
                cmn, cmx = widths[child]
                mn += cmn
                mx = None if mx is None or cmx is None else mx + cmx
            w = (mn, mx)
        elif kind == 'ONEOF':
            mn = None
            mx = 0
            for child in node[1:]:
                cmn, cmx = widths[child]
                mn = cmn if mn is None else min(mn, cmn)
                mx = None if mx is None or cmx is None else max(mx, cmx)
            w = (mn or 0, mx)
        elif kind == 'REPEAT':
            count = node[2] if len(node) == 3 else 0
            w = (count * widths[node[1]][0], None)
        elif kind == 'OPTIONAL':
            w = (0, widths[node[1]][1])
        else:
            w = (1, 1)
        widths.append(w)

    for i, node in enumerate(nodes):
        kind = node[0]
        if kind == 'REPEAT' and widths[node[1]][0] < 1:
            raise ValueError(
                f"REPEAT subnode at node {i} must consume at least one"
                f" element (min_width={widths[node[1]][0]})")
        if kind in ('FIELD_POS', 'FIELD_KW') and widths[node[1]] != (1, 1):
            raise ValueError(
                f"FIELD at node {i} subnode must have width exactly 1")
        if kind == 'CLASS':
            saw_keyword = False
            for child in node[2:]:
                if nodes[child][0] == 'FIELD_KW':
                    saw_keyword = True
                elif saw_keyword:
                    raise ValueError(
                        f"CLASS at node {i} has positional FIELD {child}"
                        f" after a keyword FIELD")

    # a FIELD or a MAP_ENTRY belongs to a chain, never to a spine
    for i, node in enumerate(nodes):
        kind = node[0]
        if kind in ('FIELD_POS', 'FIELD_KW') and i not in claimed:
            raise ValueError(f"FIELD at node {i} is not referenced by any CLASS")
        if kind.startswith('MAP_ENTRY') and i not in claimed:
            raise ValueError(f"MAP_ENTRY at node {i} is not referenced by any MAP")

    # no capture under a NOT; iterative over the subtree
    for i, node in enumerate(nodes):
        if node[0] != 'NOT':
            continue
        stack = [node[1]]
        while stack:
            j = stack.pop()
            sub = nodes[j]
            if sub[0] == 'CAPTURE':
                raise ValueError(
                    f"NOT at node {i} contains a CAPTURE in its subtree")
            stack.extend(spine_children(nodes, j))

    # each branch: at most one REST, and only fixed-width chunks after it
    for i, node in enumerate(nodes):
        kind = node[0]
        if kind in ('FORM', 'APP', 'REPEAT', 'NOT'):
            roots = [node[1]] if node[1] != -1 else []
        elif kind == 'ONEOF':
            roots = list(node[1:])
        elif kind in ('FIELD_POS', 'FIELD_KW'):
            roots = [node[1]]
        elif kind.startswith('MAP_ENTRY'):
            roots = [node[2]] if node[2] != -1 else []
        else:
            continue
        for root in roots:
            check_branch(nodes, widths, root)
    if total:
        check_branch(nodes, widths, entry)
    for i, node in enumerate(nodes):
        if node[0] in ('FIELD_POS', 'FIELD_KW') and i == entry:
            raise ValueError("FIELD at node %d may not appear on a spine" % i)


def spine_children(nodes, idx):
    """
    The nodes under a node, for a walk of its subtree
    """

    node = nodes[idx]
    kind = node[0]
    if kind == 'CAPTURE':
        return [node[2]] if node[2] != -1 else []
    if kind in ('MATCH', 'GROUP', 'ONEOF', 'MAP'):
        return list(node[1:])
    if kind == 'CLASS':
        return list(node[2:])
    if kind in ('FORM', 'OPTIONAL', 'NOT', 'REPEAT', 'FIELD_POS', 'FIELD_KW'):
        return [node[1]]
    if kind == 'APP' or kind.startswith('MAP_ENTRY'):
        return [node[2]] if node[2] != -1 else []
    return []


def branch_chunks(nodes, widths, root):
    """
    The chunks of a branch in spine order, each (min, max, is_rest):
    the branch's chunk list, collected without recursion
    """

    chunks = []
    stack = [root]
    while stack:
        idx = stack.pop()
        node = nodes[idx]
        kind = node[0]
        if kind == 'CAPTURE':
            if node[2] == -1:
                chunks.append((1, 1, False))
            else:
                stack.append(node[2])
        elif kind in ('MATCH', 'GROUP'):
            stack.extend(reversed(node[1:]))
        elif kind in ('TEST', 'SAME', 'FORM', 'CLASS', 'APP', 'MAP'):
            chunks.append((1, 1, False))
        elif kind in ('ONEOF', 'REPEAT', 'NOT'):
            mn, mx = widths[idx]
            chunks.append((mn, mx, False))
        elif kind == 'OPTIONAL':
            has_rest = False
            inner = [node[1]]
            while inner:
                j = inner.pop()
                if nodes[j][0] == 'REST':
                    has_rest = True
                    break
                inner.extend(spine_children(nodes, j))
            chunks.append((0, widths[idx][1], has_rest))
        elif kind == 'REST':
            chunks.append((node[1], None, True))
        elif kind in ('FIELD_POS', 'FIELD_KW'):
            raise ValueError(
                f"FIELD at node {idx} may not appear on a spine branch")
        elif kind.startswith('MAP_ENTRY'):
            raise ValueError(
                f"MAP_ENTRY at node {idx} may not appear on a spine branch")
    return chunks


def check_branch(nodes, widths, root):
    chunks = branch_chunks(nodes, widths, root)
    rests = [i for i, c in enumerate(chunks) if c[2]]
    if len(rests) > 1:
        raise ValueError(
            f"branch at node {root} has {len(rests)} REST nodes"
            f" (max 1 per branch)")
    if rests:
        for i in range(rests[0] + 1, len(chunks)):
            mn, mx, _ = chunks[i]
            if mn != mx:
                raise ValueError(
                    f"branch at node {root} has non-deterministic chunk"
                    f" after REST (chunk {i}: min={mn} max={mx})")


def pack_widths(mn, mx):
    """
    The packed widths of a FORM row: eight bits each, saturating, with
    0xFF for an unbounded or saturated max
    """

    mn = min(mn, LIMIT)
    mx = UNBOUNDED if mx is None or mx >= UNBOUNDED else mx
    return (mn << 8) | mx


class FragmentTarget:
    """
    The inline target: the instructions go into a fragment of the
    standard compiler, which names the slots and lowers them to fast
    indices itself
    """

    standalone = False


    def __init__(self, compiler, fragment):
        self.compiler = compiler
        self.fragment = fragment


    def instruction(self, op, arg):
        self.fragment.instruction(op, arg)


    def label(self, text):
        return self.compiler.gen_label(text)


    def declare(self, name):
        self.fragment.declare_var(name)


# the simple-style VAR instructions the standalone target lowers to
# their FAST twins, beside the MATCH_* twins named by their spelling
SIMPLE_TWINS = {
    OpCode.VAR: OpCode.FAST,
    OpCode.VAR_STORE: OpCode.FAST_STORE,
    OpCode.VAR_CLEAR: OpCode.FAST_CLEAR,
    OpCode.VAR_LIST_APPEND: OpCode.FAST_LIST_APPEND,
    OpCode.CALL_VAR: OpCode.CALL_FAST,
}


class FunctionTarget:
    """
    The standalone target: an instruction list, labels and locals of
    its own, built into a Function over a namespace the externs
    resolve in
    """

    standalone = True


    def __init__(self, name):
        self.name = name
        self.instructions = []
        self.locals = []
        self.counter = 0


    def instruction(self, op, arg):
        self.instructions.append((op, arg))


    def label(self, text):
        self.counter += 1
        return f"{text}_{self.counter:x}"


    def declare(self, name):
        if name not in self.locals:
            self.locals.append(name)


    def build(self, externs, base_ns):
        """
        The Function: the subject its one formal, the declared names
        its locals, the externs its closure names resolved in base_ns

        :param externs: the extern atoms, in extern-table order
        :param base_ns: the namespace the externs resolve in
        """

        local_syms = (SUBJECT,) + tuple(str(n) for n in self.locals)
        closure_syms = tuple(str(e) for e in externs)
        findex = list(local_syms) + list(closure_syms)

        def idx(name):
            return findex.index(str(name))

        lowered = []
        for op, arg in self.instructions:
            name = op.name
            if op in SIMPLE_TWINS:
                if op.extra:
                    arg = tuple(
                        idx(v) if fk == OpStyle.VAR else v
                        for v, fk in zip(arg, op.extra))
                else:
                    arg = idx(arg)
                op = SIMPLE_TWINS[op]
            elif name.startswith('MATCH_') and 'VAR' in name.split('_'):
                twin = OpCode[name.replace('VAR', 'FAST')]
                if op.extra:
                    arg = tuple(
                        idx(v) if fk == OpStyle.VAR else v
                        for v, fk in zip(arg, op.extra))
                else:
                    arg = idx(arg)
                op = twin
            elif op in (OpCode.MATCH_CAPTURES, OpCode.MATCH_ITERATION):
                arg = (arg[0], idx(arg[1]), idx(arg[2]), idx(arg[3]))
            lowered.append((op, arg))

        nspec = NamespaceSpec(self.name, local_syms, closure_syms)
        code = Code(nspec, lowered, self.name)
        return Function(code, base_ns, FormalsSpec((SUBJECT,)))


def lower_clause(
        compiler,
        fragment,
        frozen,
        atoms,
        miss,
        cursors,
        source,
        position=None):
    """
    Emit the instructions that match the subject on top of the stack
    against the frozen matcher, jumping to miss with the subject still
    on the stack when it does not match. Captures store into the slot
    names; externs are read from the atoms the pattern wrote.

    :param compiler: the compiler, for labels
    :param fragment: the fragment to emit into
    :param frozen: the frozen matcher 6-tuple, whose entry is a MATCH
        of one width-1 node
    :param atoms: the extern names mapped to the atoms the pattern
        wrote them as
    :param miss: the clause's miss label
    :param cursors: the cursor temporaries declared so far for this
        match form, one per nesting level, grown as needed
    :param source: the pattern form, for errors
    :param position: the pattern's position, for errors
    :return: a function the caller invokes once the clause body has
        been emitted, which emits the miss edges a nested peek needs:
        a label, the pops down to the subject, and the jump to miss
    """

    lowering = MatchLowering(
        FragmentTarget(compiler, fragment), frozen, atoms, cursors,
        source, position)
    lowering.lower_root(miss)
    return lowering.flush_trampolines


def lower_matcher(target, frozen, atoms, source=None, position=None):
    """
    Emit a whole matcher as a function body into either target: the
    subject formal, declared by the caller as the body's first local
    under the name SUBJECT, walked as a spine, the captures returned on
    a match, None on a miss. Into a fragment of the standard compiler
    the body becomes a lambda's; into a FunctionTarget it becomes a
    Function with no compiler in reach
    """

    lowering = MatchLowering(target, frozen, atoms, [], source, position)
    lowering.lower_function()


# the closure names a dispatcher reads: the values type its result is
# built with, and each clause a module compiled to a function of its own
VALUES = "values"


def clause_name(index):
    return f"clause-{index}"


def compile_dispatch_function(clauses, base_ns, name="dispatch"):
    """
    The clauses of a syntax as one Function over base_ns: one
    positional formal, the subject; the clauses tried in order; the
    result (values index captures) for the first that matches, None
    when none does. A frozen clause lowers inline, its externs closure
    names resolved in base_ns; a clause a module compiled to a
    function is called, and is stored into base_ns under the closure
    name the call reads. The values type is stored there as well

    :param clauses: frozen matcher 6-tuples and Functions, in try order
    :param base_ns: the namespace the closure names resolve in
    :param name: the function's name
    """

    target = FunctionTarget(name)
    cursors = []
    externs = []
    seen = set()
    base_ns.store(VALUES, values)

    for index, clause in enumerate(clauses):
        # the next clause's entry, or the miss tail after the last
        after = target.label("dispatch.next")
        if isinstance(clause, tuple):
            lowering = MatchLowering(target, clause, {}, cursors, None, None)
            for extern in lowering.externs:
                if extern not in seen:
                    seen.add(extern)
                    externs.append(extern)
            lowering.lower_body(after, index)
            target.instruction(OpCode.CALL_VAR, (symbol(VALUES), 2, 0, 0))
            target.instruction(OpCode.RETURN, 1)
            lowering.flush_trampolines()
            target.instruction(OpCode.LABEL, after)
        else:
            called = symbol(clause_name(index))
            base_ns.store(str(called), clause)
            seen.add(called)
            externs.append(called)
            target.instruction(OpCode.CONST, index)
            target.instruction(OpCode.VAR, symbol(SUBJECT))
            target.instruction(OpCode.CALL_VAR, (called, 1, 0, 0))
            target.instruction(OpCode.DUP, 1)
            target.instruction(OpCode.CONST, None)
            target.instruction(
                OpCode.CHECK_COMPARE, (Compare.IS_NOT, 2, after, None))
            target.instruction(OpCode.CALL_VAR, (symbol(VALUES), 2, 0, 0))
            target.instruction(OpCode.RETURN, 1)
            target.instruction(OpCode.LABEL, after)
            target.instruction(OpCode.POP, 2)

    target.instruction(OpCode.CONST, None)
    target.instruction(OpCode.RETURN, 1)
    return target.build(externs + [symbol(VALUES)], base_ns)


def compile_match_function(frozen, base_ns, atoms=None, name="matcher"):
    """
    The frozen matcher as a Function over base_ns: one positional
    formal, the subject; the externs closure names resolved in base_ns;
    the captures returned on a match and None on a miss

    :param frozen: the frozen matcher 6-tuple
    :param base_ns: the namespace the externs resolve in
    :param atoms: the extern names mapped to the atoms the pattern
        wrote them as, when a template wrote them; None reads each by
        its bare name
    :param name: the function's name
    """

    atoms = atoms or {}
    externs = [atoms.get(n) or symbol(n) for n in frozen[2]]
    target = FunctionTarget(name)
    lower_matcher(target, frozen, atoms)
    return target.build(externs, base_ns)


class MatchLowering:
    """
    The emission state for one matcher: the frozen tables, the slot
    scopes and their locals, the cursor and position temporaries, and
    the miss trampolines that pop what a nested peek pushed before
    jumping on
    """

    def __init__(self, target, frozen, atoms, cursors, source, position):
        _tag, slots, externs, consts, nodes, entry = frozen
        self.target = target
        self.slot_names = list(slots)
        self.externs = [atoms.get(name) or symbol(name) for name in externs]
        self.consts = consts
        self.nodes = nodes
        self.entry = entry
        self.cursors = cursors
        self.marks = []
        self.source = source
        self.position = position
        self.trampolines = {}
        self.widths_cache = {}
        self.scopes = {}
        self.scope_slots = {}
        self.locals = {}
        # an inline match clause binds its root captures under their
        # own names, for the user; every other lowering keeps them in
        # locals of its own and returns them by name
        self.plain_root_locals = False
        self.assign_scopes(entry, None)


    # --- bookkeeping ---


    def gap(self, what):
        return SpecialError(
            f"match pattern {what} is not lowered yet",
            self.source, position=self.position)


    def emit(self, op, *args):
        if len(args) == 1:
            self.target.instruction(op, args[0])
        else:
            args = tuple(args) + (0,) * (4 - len(args))
            self.target.instruction(op, args)


    def temporary(self, pool, level, stem):
        """
        The temporary for a nesting level, declared on first use
        """

        while len(pool) <= level:
            name = gensym(stem)
            self.target.declare(name)
            pool.append(name)
        return pool[level]


    def cursor(self, level):
        return self.temporary(self.cursors, level, "match-cursor")


    def mark(self, level):
        return self.temporary(self.marks, level, "match-mark")


    def assign_scopes(self, idx, scope):
        """
        Record the capture scope of every slot: the root, or the GROUP
        or REPEAT node whose captures it belongs to
        """

        node = self.nodes[idx]
        kind = node[0]
        if kind == 'CAPTURE':
            self.scopes[node[1]] = scope
            self.scope_slots.setdefault(scope, []).append(node[1])
            if node[2] != -1:
                self.assign_scopes(node[2], scope)
        elif kind == 'GROUP':
            self.scope_slots.setdefault(idx, [])
            for child in node[1:]:
                self.assign_scopes(child, idx)
        elif kind == 'REPEAT':
            if self.nodes[node[1]][0] == 'GROUP':
                # the group under a repeat is the iteration's scope
                self.assign_scopes(node[1], scope)
            else:
                self.scope_slots.setdefault(idx, [])
                self.assign_scopes(node[1], idx)
        elif kind in ('MATCH', 'ONEOF'):
            for child in node[1:]:
                self.assign_scopes(child, scope)
        elif kind in ('FORM', 'OPTIONAL', 'NOT'):
            self.assign_scopes(node[1], scope)
        elif kind == 'APP':
            if node[2] != -1:
                self.assign_scopes(node[2], scope)
        elif kind == 'CLASS':
            for field in node[2:]:
                self.assign_scopes(self.nodes[field][1], scope)
        elif kind == 'MAP':
            for entry in node[1:]:
                sub = self.nodes[entry][2]
                if sub != -1:
                    self.assign_scopes(sub, scope)
        elif kind in ('FIELD_POS', 'FIELD_KW'):
            self.assign_scopes(node[1], scope)


    def scope_of(self, idx):
        """
        The scope a REPEAT or GROUP node's captures belong to
        """

        node = self.nodes[idx]
        if node[0] == 'REPEAT' and self.nodes[node[1]][0] == 'GROUP':
            return node[1]
        return idx


    def slot_local(self, slot):
        """
        The local a slot is stored in: the slot's own name at the root
        of an inline clause, where the match form binds it for the
        user; otherwise a name of its own, so a group's slot never
        collides with another
        """

        found = self.locals.get(slot)
        if found is None:
            name = self.slot_names[slot]
            if self.scopes.get(slot) is None and self.plain_root_locals:
                found = symbol(name)
            else:
                found = gensym(name)
            self.target.declare(found)
            self.locals[slot] = found
        return found


    def scope_table(self, scope):
        """
        The captures table of a scope for the standalone target: the
        names tuple and the first local, the locals of the scope's
        slots having been declared in slot order; for the inline target
        the names tuple and the locals tuple
        """

        slots = self.scope_slots.get(scope, [])
        names = tuple(self.slot_names[s] for s in slots)
        locals_ = tuple(self.slot_local(s) for s in slots)
        return names, locals_


    def open_scope(self, scope):
        """
        Declare a scope's slot locals together, in slot order, before
        anything under it emits, so the standalone target's fast slots
        for them are one run
        """

        self.scope_table(scope)


    def clear_slots(self, idx):
        """
        Clear every capture written under a node, on the edge where
        its attempt failed and the pattern goes on
        """

        for slot in self.slots_under(idx):
            self.emit(OpCode.VAR_CLEAR, self.slot_local(slot))


    def slots_under(self, idx):
        node = self.nodes[idx]
        kind = node[0]
        out = []
        if kind == 'CAPTURE':
            out.append(node[1])
            if node[2] != -1:
                out.extend(self.slots_under(node[2]))
        elif kind in ('MATCH', 'GROUP', 'ONEOF'):
            for child in node[1:]:
                out.extend(self.slots_under(child))
        elif kind in ('FORM', 'OPTIONAL', 'NOT', 'REPEAT'):
            out.extend(self.slots_under(node[1]))
        elif kind == 'APP':
            if node[2] != -1:
                out.extend(self.slots_under(node[2]))
        elif kind == 'CLASS':
            for field in node[2:]:
                out.extend(self.slots_under(self.nodes[field][1]))
        elif kind == 'MAP':
            for entry in node[1:]:
                sub = self.nodes[entry][2]
                if sub != -1:
                    out.extend(self.slots_under(sub))
        return out


    def miss_at(self, miss, depth):
        """
        The label a miss at depth jumps to: the miss label itself when
        nothing is stacked above its base, else a trampoline that pops
        the excess first
        """

        label, base = miss
        if depth == base:
            return label
        key = (label, depth - base)
        found = self.trampolines.get(key)
        if found is None:
            found = self.target.label("match.pop")
            self.trampolines[key] = found
        return found


    def flush_trampolines(self):
        for (label, pops), tramp in self.trampolines.items():
            self.emit(OpCode.LABEL, tramp)
            self.emit(OpCode.POP, pops)
            self.emit(OpCode.JUMP, label)
        self.trampolines.clear()


    def children(self, idx):
        node = self.nodes[idx]
        if node[0] in ('MATCH', 'GROUP'):
            return list(node[1:])
        return [idx]


    def width(self, idx):
        """
        The min and max width of a node, max None when unbounded
        """

        found = self.widths_cache.get(idx)
        if found is not None:
            return found

        node = self.nodes[idx]
        kind = node[0]
        if kind == 'CAPTURE':
            w = (1, 1) if node[2] == -1 else self.width(node[2])
        elif kind in ('TEST', 'FORM', 'APP', 'MAP', 'CLASS', 'SAME'):
            w = (1, 1)
        elif kind == 'NOT':
            w = (0, 0)
        elif kind == 'REST':
            w = (node[1], None)
        elif kind in ('MATCH', 'GROUP'):
            mn = 0
            mx = 0
            for child in node[1:]:
                cmn, cmx = self.width(child)
                mn += cmn
                mx = None if mx is None or cmx is None else mx + cmx
            w = (mn, mx)
        elif kind == 'ONEOF':
            mn = None
            mx = 0
            for child in node[1:]:
                cmn, cmx = self.width(child)
                mn = cmn if mn is None else min(mn, cmn)
                mx = None if mx is None or cmx is None else max(mx, cmx)
            w = (mn or 0, mx)
        elif kind == 'REPEAT':
            w = (node[2] * self.width(node[1])[0], None)
        elif kind == 'OPTIONAL':
            w = (0, self.width(node[1])[1])
        else:
            w = (1, 1)

        self.widths_cache[idx] = w
        return w


    def is_bare_capture(self, idx):
        node = self.nodes[idx]
        return node[0] == 'CAPTURE' and node[2] == -1


    def is_type_test(self, idx):
        node = self.nodes[idx]
        return node[0] == 'TEST' and node[1] == 'TYPE'


    def is_core_test(self, idx):
        node = self.nodes[idx]
        return node[0] == 'TEST' and node[1] == 'CORE'


    # --- entry points ---


    def lower_root(self, miss):
        """
        The entry node against the subject on top of the stack, for
        an inline clause
        """

        entry = self.nodes[self.entry]
        if entry[0] != 'MATCH' or len(entry) != 2:
            raise self.gap("root shape")
        child = entry[1]
        if self.width(child) != (1, 1):
            raise self.gap("root width")
        self.plain_root_locals = True
        self.emit_peek(child, (miss, 0), 0)


    def lower_function(self):
        """
        The entry node against the subject formal walked as a spine,
        for the standalone target: the captures returned on a match,
        None on a miss
        """

        miss = self.target.label("match.miss")
        self.lower_body(miss)
        self.emit(OpCode.RETURN, 1)
        self.flush_trampolines()
        self.emit(OpCode.LABEL, miss)
        self.emit(OpCode.CONST, None)
        self.emit(OpCode.RETURN, 1)


    def lower_body(self, miss, index=None):
        """
        The entry node against the subject formal walked as a spine,
        leaving the captures on the stack on a match and jumping to
        miss with nothing left on it otherwise. With an index, the
        clause's index in its dispatcher is pushed under the captures,
        the two arguments of the values the dispatcher returns
        """

        cur = self.cursor(0)
        start = self.mark(0)
        # the root scope's locals are declared first so their fast
        # slots are one run the captures table can name
        for slot in self.scope_slots.get(None, []):
            self.slot_local(slot)

        self.emit(OpCode.VAR, symbol(SUBJECT))
        self.emit(
            OpCode.MATCH_FORM_VAR, self.miss_at((miss, 0), 1), cur,
            pack_widths(*self.width(self.entry)))
        self.emit(OpCode.POP, 1)
        self.emit(OpCode.MATCH_MARK_VAR, cur, start)
        self.emit_pull(self.entry, cur, (miss, 0), 0, 1, 0)
        self.emit(OpCode.MATCH_END_VAR, miss, cur)
        if index is not None:
            self.emit(OpCode.CONST, index)
        self.emit_captures(None, start, cur)


    def emit_captures(self, scope, start, cur, iteration=False):
        """
        Push the captures of a scope, or one repeat iteration's value
        """

        names, locals_ = self.scope_table(scope)
        if self.target.standalone:
            op = OpCode.MATCH_ITERATION if iteration else OpCode.MATCH_CAPTURES
            first = locals_[0] if locals_ else cur
            self.emit(op, names, first, start, cur)
        else:
            # the locals travel as spellings: the table is a constant,
            # which the operand lowering never touches, and the handler
            # reads a namespace by spelling
            op = (OpCode.MATCH_ITERATION_NAMED_VAR if iteration
                  else OpCode.MATCH_CAPTURES_NAMED_VAR)
            spelled = tuple(str(local) for local in locals_)
            self.emit(op, (names, spelled), start, cur)


    # --- peek mode ---


    def emit_peek(self, idx, miss, depth, level=0):
        """
        Test the element on top of the stack against the node, never
        popping it; depth is the count of elements the pattern has
        stacked above the clause's subject, level the count of spines
        open around it, whose cursors a descent must not take
        """

        node = self.nodes[idx]
        kind = node[0]
        m = self.miss_at(miss, depth)

        if kind == 'TEST':
            self.emit_peek_test(node, miss, depth)

        elif kind == 'CAPTURE':
            slot = self.slot_local(node[1])
            sub = node[2]
            if sub == -1:
                self.emit(OpCode.MATCH_BIND_VAR, slot)
            elif self.is_type_test(sub):
                type_ = self.externs[self.nodes[sub][2]]
                self.emit(OpCode.MATCH_TYPE_BIND_VAR, m, type_, slot)
            elif self.is_core_test(sub):
                self.emit(
                    OpCode.MATCH_CORE_TYPE_BIND_VAR, m, self.nodes[sub][2], slot)
            else:
                self.emit_peek(sub, miss, depth, level)
                self.emit(OpCode.MATCH_BIND_VAR, slot)

        elif kind == 'SAME':
            if node[2] > 1:
                self.emit(OpCode.JUMP, m)
            else:
                self.emit(OpCode.MATCH_SAME_VAR, m, self.slot_local(node[1]))

        elif kind == 'FORM':
            sub = node[1]
            cur = self.cursor(level)
            self.emit(
                OpCode.MATCH_FORM_VAR, m, cur, pack_widths(*self.width(sub)))
            self.emit_sequence(
                self.children(sub), cur, miss, depth, level + 1, 0)
            self.emit(OpCode.MATCH_END_VAR, m, cur)

        elif kind == 'ONEOF':
            branches = node[1:]
            join = self.target.label("match.or")
            for branch in branches[:-1]:
                nxt = self.target.label("match.or")
                self.emit_peek(branch, (nxt, depth), depth, level)
                self.emit(OpCode.JUMP, join)
                self.emit(OpCode.LABEL, nxt)
                self.clear_slots(branch)
            if branches:
                self.emit_peek(branches[-1], miss, depth, level)
            self.emit(OpCode.LABEL, join)

        elif kind == 'OPTIONAL':
            ok = self.target.label("match.opt")
            self.emit_peek(node[1], (ok, depth), depth, level)
            self.emit(OpCode.LABEL, ok)

        elif kind == 'NOT':
            ok = self.target.label("match.not")
            self.emit_peek(node[1], (ok, depth), depth, level)
            self.emit(OpCode.JUMP, m)
            self.emit(OpCode.LABEL, ok)

        elif kind == 'MATCH' and self.width(idx) == (1, 1):
            # a one-element sequence against the element itself: the
            # registry heads wrap their operand so, and the case
            # dialect's (not p) is a zero-width NOT then an any; each
            # zero-width child peeks the same element
            for child in node[1:]:
                if self.width(child) not in ((0, 0), (1, 1)):
                    raise self.gap("sequence shape")
                self.emit_peek(child, miss, depth, level)

        elif kind == 'CLASS':
            self.emit_peek_class(node, miss, depth, level)

        elif kind == 'MAP':
            self.emit(OpCode.MATCH_MAP, m)
            for entry in node[1:]:
                self.emit_peek_entry(self.nodes[entry], miss, depth, level)

        elif kind == 'APP':
            fn = self.externs[node[1]]
            self.emit(OpCode.DUP, 1)
            self.emit(OpCode.CALL_VAR, fn, 1)
            if node[2] != -1:
                self.emit_peek(node[2], miss, depth + 1, level)
            self.emit(OpCode.POP, 1)

        else:
            raise self.gap(kind)


    def emit_peek_test(self, node, miss, depth):
        m = self.miss_at(miss, depth)
        test = node[1]
        if test == 'ANY':
            return
        if test == 'LITERAL':
            self.emit(OpCode.MATCH_LITERAL, m, self.consts[node[2]])
        elif test == 'TYPE':
            self.emit(OpCode.MATCH_TYPE_VAR, m, self.externs[node[2]])
        elif test == 'CORE':
            self.emit(OpCode.MATCH_CORE_TYPE, m, node[2])
        elif test == 'PREDICATE':
            self.emit(OpCode.DUP, 1)
            self.emit(OpCode.CALL_VAR, self.externs[node[2]], 1)
            self.emit(OpCode.JUMP_FALSE, m)
        else:
            raise self.gap(f"test {test}")


    def emit_peek_class(self, node, miss, depth, level):
        m = self.miss_at(miss, depth)
        cls = self.externs[node[1]]
        fields = [self.nodes[f] for f in node[2:]]
        npos = sum(1 for f in fields if f[0] == 'FIELD_POS')
        self.emit(OpCode.MATCH_CLASS_VAR, m, cls, npos)

        pos = 0
        for field in fields:
            sub = field[1]
            if field[0] == 'FIELD_POS':
                if self.is_bare_capture(sub):
                    self.emit(
                        OpCode.MATCH_FIELD_POS_BIND_VAR,
                        m, cls, pos, self.slot_local(self.nodes[sub][1]))
                else:
                    self.emit(OpCode.MATCH_FIELD_POS_VAR, m, cls, pos)
                    self.emit_peek(sub, miss, depth + 1, level)
                    self.emit(OpCode.POP, 1)
                pos += 1
            else:
                name = field[2]
                if self.is_bare_capture(sub):
                    self.emit(
                        OpCode.MATCH_FIELD_KW_BIND_VAR,
                        m, name, self.slot_local(self.nodes[sub][1]))
                else:
                    self.emit(OpCode.MATCH_FIELD_KW, m, name)
                    self.emit_peek(sub, miss, depth + 1, level)
                    self.emit(OpCode.POP, 1)


    def emit_peek_entry(self, entry, miss, depth, level):
        m = self.miss_at(miss, depth)
        if entry[0] == 'MAP_ENTRY_CONST':
            key = self.consts[entry[1]]
            push, bind = OpCode.MATCH_KEY_CONST, OpCode.MATCH_KEY_CONST_BIND_VAR
        else:
            key = self.externs[entry[1]]
            push, bind = OpCode.MATCH_KEY_VAR, OpCode.MATCH_KEY_VAR_BIND_VAR
        sub = entry[2]
        if sub != -1 and self.is_bare_capture(sub):
            self.emit(bind, m, key, self.slot_local(self.nodes[sub][1]))
            return
        self.emit(push, m, key)
        if sub != -1:
            self.emit_peek(sub, miss, depth + 1, level)
        self.emit(OpCode.POP, 1)


    # --- pull mode ---


    def emit_sequence(self, children, cur, miss, depth, level, trailing):
        """
        The children of a spine in order, each pulled from cur, with
        the minimum width of the siblings after it, and of whatever
        follows the spine, as its trailing reservation
        """

        mins = [self.width(c)[0] for c in children]
        for i, child in enumerate(children):
            after = sum(mins[i + 1:]) + trailing
            self.emit_pull(child, cur, miss, depth, level, after)


    def rest_counts(self, min_tail, trailing):
        if min_tail > LIMIT or trailing > LIMIT:
            raise self.gap("rest counts past 255")
        return (min_tail << 8) | trailing


    def guard(self, idx, cur, label, trailing):
        """
        The reservation check before an attempt that could consume
        into the suffix: the node's minimum width plus the trailing
        reservation must be available, else jump to label
        """

        if trailing <= 0:
            return
        needed = self.width(idx)[0] + trailing
        self.emit(OpCode.MATCH_AVAILABLE_VAR, label, cur, needed)


    def emit_pull(self, idx, cur, miss, depth, level, trailing):
        """
        One node pulled from the cursor cur; level counts the open
        spines for the cursor and mark temporaries
        """

        node = self.nodes[idx]
        kind = node[0]
        m = self.miss_at(miss, depth)

        if kind == 'CAPTURE':
            self.emit_pull_capture(idx, cur, miss, depth, level, trailing)

        elif kind == 'TEST':
            test = node[1]
            if test == 'ANY':
                self.emit(OpCode.MATCH_NEXT_VAR, m, cur)
                self.emit(OpCode.POP, 1)
            elif test == 'LITERAL':
                self.emit(
                    OpCode.MATCH_NEXT_LITERAL_VAR, m, cur, self.consts[node[2]])
            elif test == 'TYPE':
                self.emit(
                    OpCode.MATCH_NEXT_TYPE_VAR, m, cur, self.externs[node[2]])
            elif test == 'CORE':
                self.emit(OpCode.MATCH_NEXT_CORE_TYPE_VAR, m, cur, node[2])
            else:
                self.emit(OpCode.MATCH_NEXT_VAR, m, cur)
                self.emit_peek_test(node, miss, depth + 1)
                self.emit(OpCode.POP, 1)

        elif kind == 'SAME':
            if node[2] > 1:
                self.emit(OpCode.JUMP, m)
            else:
                self.emit(
                    OpCode.MATCH_NEXT_SAME_VAR, m, cur,
                    self.slot_local(node[1]))

        elif kind == 'REST':
            counts = self.rest_counts(node[1], trailing)
            self.emit(OpCode.MATCH_REST_VAR, m, cur, counts)

        elif kind == 'FORM':
            sub = node[1]
            inner = self.cursor(level)
            self.emit(
                OpCode.MATCH_NEXT_FORM_VAR,
                m, cur, inner, pack_widths(*self.width(sub)))
            self.emit_sequence(
                self.children(sub), inner, miss, depth, level + 1, 0)
            self.emit(OpCode.MATCH_END_VAR, m, inner)

        elif kind == 'MATCH':
            self.emit_sequence(
                list(node[1:]), cur, miss, depth, level, trailing)

        elif kind == 'GROUP':
            self.emit_pull_group(idx, cur, miss, depth, level, trailing)

        elif kind == 'REPEAT':
            self.emit_pull_repeat(
                idx, None, cur, miss, depth, level, trailing)

        elif kind == 'OPTIONAL':
            self.emit_pull_optional(idx, cur, miss, depth, level, trailing)

        elif kind == 'ONEOF':
            self.emit_pull_oneof(idx, cur, miss, depth, level, trailing)

        elif kind == 'NOT':
            # a NOT consumes nothing: the next element is read without
            # advancing the cursor, and an end is a pass
            ok = self.target.label("match.not")
            self.emit(OpCode.MATCH_PEEK_VAR, ok, cur)
            self.emit_peek(node[1], (ok, depth), depth + 1, level)
            self.emit(OpCode.POP, 1)
            self.emit(OpCode.JUMP, m)
            self.emit(OpCode.LABEL, ok)

        elif kind in ('CLASS', 'MAP', 'APP'):
            self.emit(OpCode.MATCH_NEXT_VAR, m, cur)
            self.emit_peek(idx, miss, depth + 1, level)
            self.emit(OpCode.POP, 1)

        else:
            raise self.gap(kind)


    def emit_pull_capture(self, idx, cur, miss, depth, level, trailing):
        node = self.nodes[idx]
        m = self.miss_at(miss, depth)
        slot = self.slot_local(node[1])
        sub = node[2]

        if sub == -1:
            self.emit(OpCode.MATCH_NEXT_BIND_VAR, m, cur, slot)
            return

        subnode = self.nodes[sub]
        if self.is_type_test(sub):
            type_ = self.externs[subnode[2]]
            self.emit(OpCode.MATCH_NEXT_TYPE_BIND_VAR, m, cur, type_, slot)
        elif self.is_core_test(sub):
            self.emit(
                OpCode.MATCH_NEXT_CORE_TYPE_BIND_VAR, m, cur, subnode[2], slot)
        elif subnode[0] == 'REST':
            counts = self.rest_counts(subnode[1], trailing)
            self.emit(OpCode.MATCH_REST_BIND_VAR, m, cur, counts, slot)
        elif subnode[0] == 'REPEAT':
            self.emit_pull_repeat(sub, slot, cur, miss, depth, level, trailing)
        elif subnode[0] == 'GROUP':
            self.emit_pull_group(
                sub, cur, miss, depth, level, trailing, store=slot)
        elif self.width(sub) == (1, 1):
            self.emit(OpCode.MATCH_NEXT_VAR, m, cur)
            self.emit_peek(sub, miss, depth + 1, level)
            self.emit(OpCode.MATCH_BIND_VAR, slot)
            self.emit(OpCode.POP, 1)
        elif self.width(sub)[0] >= 1:
            # a capture over a wider node binds the span it consumed:
            # one boxed element, or a tuple of them
            start = self.mark(level)
            self.emit(OpCode.MATCH_MARK_VAR, cur, start)
            self.emit_pull(sub, cur, miss, depth, level + 1, trailing)
            self.emit_span(start, cur)
            self.emit(OpCode.VAR_STORE, slot)
        else:
            raise self.gap("capture over a node that may consume nothing")


    def emit_span(self, start, cur):
        """
        Push the span from a mark to the cursor as a capture binds it:
        the iteration value of an empty table
        """

        if self.target.standalone:
            self.emit(OpCode.MATCH_ITERATION, (), cur, start, cur)
        else:
            self.emit(OpCode.MATCH_ITERATION_NAMED_VAR, ((), ()), start, cur)


    def emit_pull_group(
            self, idx, cur, miss, depth, level, trailing, store=None):
        """
        A GROUP: its children on the same cursor, then its captures
        pushed, stored into the capturing slot or dropped
        """

        self.open_scope(idx)
        start = self.mark(level)
        self.emit(OpCode.MATCH_MARK_VAR, cur, start)
        self.emit_sequence(
            self.children(idx), cur, miss, depth, level + 1, trailing)
        self.emit_captures(idx, start, cur)
        if store is not None:
            self.emit(OpCode.VAR_STORE, store)
        else:
            self.emit(OpCode.POP, 1)


    def emit_pull_repeat(self, idx, store, cur, miss, depth, level, trailing):
        """
        A REPEAT: iterations of the inner node while they match and
        the suffix reservation allows, each iteration's value appended
        to the list in the capturing slot, or to a temporary when no
        slot captures the repeat
        """

        node = self.nodes[idx]
        inner = node[1]
        min_count = node[2]
        scope = self.scope_of(idx)
        self.open_scope(scope)
        items = store if store is not None else self.temporary(
            [], 0, "match-items")
        start = self.mark(level)
        loop = self.target.label("match.repeat")
        fail = self.target.label("match.repeat.miss")
        done = self.target.label("match.repeat.end")

        self.emit(OpCode.BUILD_LIST, 0)
        self.emit(OpCode.VAR_STORE, items)
        self.emit(OpCode.LABEL, loop)
        self.guard(inner, cur, done, trailing)
        self.emit(OpCode.MATCH_MARK_VAR, cur, start)
        self.emit_sequence(
            self.children(inner), cur, (fail, depth), depth, level + 1,
            trailing)
        # the iteration instruction clears the slots it read
        self.emit_captures(scope, start, cur, iteration=True)
        self.emit(OpCode.VAR_LIST_APPEND, items)
        self.emit(OpCode.JUMP, loop)

        self.emit(OpCode.LABEL, fail)
        self.emit(OpCode.MATCH_RESET_VAR, cur, start)
        self.clear_slots(inner)
        self.emit(OpCode.LABEL, done)

        if min_count > 0:
            self.emit(OpCode.VAR, items)
            self.emit(OpCode.OBJECT_LEN, 0)
            self.emit(OpCode.CONST, min_count)
            self.emit(
                OpCode.CHECK_COMPARE,
                int(Compare.GE), 2, self.miss_at(miss, depth), 0)


    def emit_pull_optional(self, idx, cur, miss, depth, level, trailing):
        """
        An OPTIONAL: the inner node tried, the cursor restored and its
        captures cleared when it misses
        """

        sub = self.nodes[idx][1]
        skip = self.target.label("match.opt.skip")
        done = self.target.label("match.opt")
        start = self.mark(level)
        self.guard(sub, cur, done, trailing)
        self.emit(OpCode.MATCH_MARK_VAR, cur, start)
        self.emit_pull(sub, cur, (skip, depth), depth, level + 1, trailing)
        self.emit(OpCode.JUMP, done)
        self.emit(OpCode.LABEL, skip)
        self.emit(OpCode.MATCH_RESET_VAR, cur, start)
        self.clear_slots(sub)
        self.emit(OpCode.LABEL, done)


    def emit_pull_oneof(self, idx, cur, miss, depth, level, trailing):
        """
        A ONEOF in a spine: branches of one element try the pulled
        element in turn; wider branches each start from a mark the
        next branch resets to
        """

        node = self.nodes[idx]
        branches = node[1:]
        if all(self.width(b) == (1, 1) for b in branches) and trailing == 0:
            m = self.miss_at(miss, depth)
            self.emit(OpCode.MATCH_NEXT_VAR, m, cur)
            self.emit_peek(idx, miss, depth + 1, level)
            self.emit(OpCode.POP, 1)
            return

        start = self.mark(level)
        join = self.target.label("match.or")
        self.emit(OpCode.MATCH_MARK_VAR, cur, start)
        for branch in branches[:-1]:
            nxt = self.target.label("match.or")
            self.guard(branch, cur, nxt, trailing)
            self.emit_pull(branch, cur, (nxt, depth), depth, level + 1, trailing)
            self.emit(OpCode.JUMP, join)
            self.emit(OpCode.LABEL, nxt)
            self.emit(OpCode.MATCH_RESET_VAR, cur, start)
            self.clear_slots(branch)
        if branches:
            self.guard(branches[-1], cur, self.miss_at(miss, depth), trailing)
            self.emit_pull(
                branches[-1], cur, miss, depth, level + 1, trailing)
        self.emit(OpCode.LABEL, join)


# The end.
