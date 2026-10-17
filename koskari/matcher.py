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
koskari.matcher

The matcher: the pattern builders and the registry of pattern forms,
and the compiler that lowers a pattern to the tables its matcher
function is built from

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from dataclasses import dataclass
from typing import Any, Protocol, runtime_checkable

from koskari.lib import (
    CoreType, Datum, DatumFrame, ModuleNamespace, cons, demand, keyword,
    kos_repr, nil, pair, symbol)
from koskari.match_code import compile_match_function
from koskari.match_core import analyse
from koskari.types import (
    is_keyword, is_nil, is_pair, is_proper, is_symbol, strip_mark, unpack)


__all__ = (
    "make_matcher",
    "match_pattern_raw",
    "match",
    "match1",

    "MatchResult",
    "Datum",
    "DatumFrame",
    "Pattern",
    "Composite",
    "MatchCompiler",

    "BINDING_FORMS",
    "USING_FORMS",
    "APP",
    "BIND",
    "BINDING",
    "BINDINGS",
    "BODY",
    "CALLEDBY",
    "CAPTURE",
    "CLASS",
    "DECL",
    "DEFINE",
    "FIELD",
    "FORM",
    "FORMALS",
    "GROUP",
    "KEYWORD",
    "LITERAL",
    "LITERAL_KW",
    "LITERAL_SYM",
    "LIST",
    "MAP",
    "MAP_ENTRY",
    "MATCH",
    "NOT",
    "ONEOF",
    "OPTIONAL",
    "PAIR",
    "PREDICATE",
    "REPEAT",
    "REST",
    "SAME",
    "SYMBOL",
    "TEST_ANY",
    "TYPE",
    "USING",
    "USING_UNPACK",
    "VALUE",
    "CASE_DELIM_MATCHER",
    "CASE_DELIM_PATTERN",
    "CASE_ELEMENT_MATCHER",
    "CASE_ELEMENT_PATTERN",
    "META_MATCHER",
    "META_PATTERN",

    "repeat_capture",
    "binding_inner_pattern",
    "bindings_grouped_list_cell_pattern",

    "MATCHER_REGISTRY",
    "register_matcher",
    "lookup_matcher",
    "parse_matcher",
    "parse_explicit_rule_pattern",
    "pattern_slot_names",
    "parse_case_element",
    "parse_case_pattern",
)


# === The result of Pattern.match ===


@dataclass
class MatchResult:
    """
    The outcome of a match: whether it matched, and the captures, a
    dict or a DatumFrame, sparse over the slots that fired
    """

    matched: bool
    captures: Any


# === Builder infrastructure ===


class MatchCompiler:
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
        self._slot_scopes = [({}, True, None)]
        self.in_class_fields = False


    def begin_nested_claims(self, inherit_formals=True, alternatives=None):
        """
        Begin a nested region where duplicate capture slot names are isolated
        (for example ONEOF branches or GROUP children). When
        ``inherit_formals`` is false, slots interned inside the region are
        omitted from :data:`formal_slot_names`: a GROUP or REPEAT row's
        slots are fields of the row, and a ONEOF branch's are formals

        :param alternatives: for a ONEOF branch, a dict the ONEOF
            shares across its branches; a name the branches bind has
            one slot index across them
        """

        self.capture_scope_stack.append({})
        self._slot_scopes.append(({}, not inherit_formals, alternatives))
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
        slots, frame, _alternatives = self._slot_scopes.pop()
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
        for _slots, frame, alternatives in reversed(self._slot_scopes):
            if frame:
                break
            branches.append(alternatives)
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
        for slots, frame, _alternatives in reversed(self._slot_scopes):
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


    def claim_slot_name(self, name, signature):
        """
        Reject duplicate slot bindings within the current capture scope.
        Repeat a pattern variable with :class:`SAME` instead of a second
        CAPTURE. ONEOF branches use a fresh scope per branch so alternate
        arms may reuse slot names

        :param name: capture slot name
        :param signature: hashable tuple describing this claim
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


    def lower(self, root):
        """
        Lower root into this compiler and return its tables, the
        lowering's input

        :param root: pattern to lower
        :return: (slots, externs, consts, nodes, entry)
        """

        entry = root.lower(self)
        return (
            tuple(Pattern._slot_names(self)),
            tuple(Pattern._extern_names(self)),
            tuple(self.consts),
            tuple(self.nodes),
            entry,
        )


@runtime_checkable
class Pattern(Protocol):
    """
    Structural base for matcher pattern builders. Implementations define
    lower(ctx); the compiler passes itself as ctx when lowering a root
    pattern. GROUP and ONEOF bracket nested lowering with
    begin_nested_claims and end_nested_claims. build lowers through
    MatchCompiler and compiles the matcher function
    """


    def lower(self, ctx):
        """
        Append nodes for this pattern into the compiler context and
        return the index of the resulting top node
        """

        ...


    def build(self, externs=None):
        """
        Compile this pattern to its matcher function: one positional
        formal, the subject; the captures on a match, None on a miss.
        Its externs are closure names resolved in a namespace whose
        builtins are the externs mapping

        :param externs: the extern names mapped to their values, or None
        :raises TypeError: when a LITERAL value or a MAP entry key is
            not one the constant pool holds, or an extern the pattern
            declares is not given
        :raises ValueError: when the pattern is one the core refuses
        :rtype: Function
        """

        tables = MatchCompiler().lower(self)
        analyse(tables)
        externs = dict(externs or {})
        for name in tables[1]:
            if name not in externs:
                raise TypeError(f"matcher missing extern binding: {name!r}")
        return compile_match_function(
            tables, ModuleNamespace("matcher", externs))


    def match(self, subject, /, **externs):
        """
        Build this pattern over the keyword externs and run it against
        subject. Returns the MatchResult
        """

        captures = demand(self.build(externs))(subject)
        if captures is None:
            return MatchResult(False, {})
        return MatchResult(True, captures)


    @staticmethod
    def _slot_names(ctx):
        """
        Return slot names in declaration order
        """

        return list(ctx.slot_names)


    @staticmethod
    def _extern_names(ctx):
        """
        Return extern names in declaration order
        """

        return list(ctx.extern_indices.keys())


def check_pattern_arg(arg, what):
    """
    Validate that arg is a Pattern instance and raise a clear TypeError
    when it is not
    """

    if not isinstance(arg, Pattern):
        raise TypeError(
            f"{what} must be a matcher pattern, got {type(arg).__name__!r}")


def check_str_arg(arg, what):
    """
    Validate that arg is a non-empty str. Raises TypeError for the
    wrong type and ValueError for the empty-string case
    """

    if not isinstance(arg, str):
        raise TypeError(
            f"{what} must be a str, got {type(arg).__name__!r}")
    if not arg:
        raise ValueError(f"{what} must be a non-empty str")


def pattern_slot_names(pattern):
    """
    Top-level macro lambda formals for ``pattern`` in declaration order

    Slots nested inside anonymous GROUP / REPEAT row frames are omitted;
    ONEOF branch captures and spine MATCH slots are included
    """

    check_pattern_arg(pattern, "pattern_slot_names pattern")
    mc = MatchCompiler()
    mc.lower(pattern)
    return list(mc.formal_slot_names)


# === Primitive builders ===


class SAME(Pattern):
    """
    Match one element equal to an earlier capture of this frame: the
    root, or the GROUP or REPEAT iteration the SAME sits in, at depth
    0; the enclosing frame at depth 1
    """


    def __init__(self, slot, depth=0):
        """
        :param slot: capture slot name; a SAME that reads it while it
            is unbound misses
        :param depth: 0 for current capture dict, 1 for parent GROUP frame
        :raises TypeError: when slot is not a str or depth is not an int
        :raises ValueError: when slot is empty or depth is negative
        """

        check_str_arg(slot, "SAME slot")
        if isinstance(depth, bool) or not isinstance(depth, int):
            raise TypeError(
                f"SAME depth must be an int, got {type(depth).__name__!r}")
        if depth < 0:
            raise ValueError(
                f"SAME depth must be non-negative, got {depth}")
        self._slot = slot
        self._depth = depth


    def lower(self, ctx):
        """
        Emit a SAME node referencing an existing slot index
        """

        slot_idx = ctx.resolve_slot(self._slot, self._depth)
        return ctx.append_node(
            ("SAME", slot_idx, self._depth))


    def __repr__(self):
        if self._depth:
            return f"SAME({self._slot!r}, depth={self._depth})"
        return f"SAME({self._slot!r})"


    def __kos_repr__(self):
        if self._depth:
            return f"(SAME {self._slot} depth: {self._depth})"
        return f"(SAME {self._slot})"


class TEST_ANY(Pattern):
    """
    Match any single element
    """


    def __init__(self):
        """
        TEST_ANY takes no parameters
        """

        pass


    def lower(self, ctx):
        """
        Emit the TEST_ANY node tuple
        """

        return ctx.append_node(("TEST", "ANY", -1))


    def __repr__(self):
        return "TEST_ANY()"


    def __kos_repr__(self):
        return "(TEST_ANY)"


class LITERAL(Pattern):
    """
    Match a single element equal to the given literal value
    """


    def __init__(self, value):
        """
        Capture the literal value to be referenced through the consts
        table when the pattern is lowered

        :param value: any concrete value, must not be a pattern; an
            atom is held by its bare spelling, since a literal names
            an atom and hygiene renames only bindings
        :raises TypeError: when value is itself a Pattern
        """

        if isinstance(value, Pattern):
            raise TypeError(
                "LITERAL value must be a concrete value, "
                f"not a pattern: {type(value).__name__!r}")
        self._value = strip_mark(value)


    def lower(self, ctx):
        """
        Intern the literal in consts and emit a TEST/LITERAL node
        """

        idx = ctx.intern_const(self._value)
        return ctx.append_node(("TEST", "LITERAL", idx))


    def __repr__(self):
        return f"LITERAL({repr(self._value)})"


    def __kos_repr__(self):
        if is_symbol(self._value):
            return str(self._value)
        if is_keyword(self._value):
            name = str(self._value).rstrip(':')
            return f'(LITERAL_KW "{name}")'
        return f"(LITERAL {kos_repr(self._value)})"


# the types a pattern names by their bare spelling and tests without a
# reference: a MATCH_CORE_TYPE instruction carries the ordinal
CORE_TYPES = {name.lower(): member for name, member in CoreType.__members__.items()}


class TYPE(Pattern):
    """
    Match a single element which is an instance of the named type: a
    core type by its ordinal, any other name as an extern resolved at
    match time
    """


    def __init__(self, extern_name):
        """
        :param extern_name: the extern binding name supplied when the
            matcher is made, e.g. make_matcher(pat, {"symbol": symbol})
        :raises TypeError: when extern_name is not a str
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "TYPE extern name")
        self._extern = extern_name


    def lower(self, ctx):
        """
        Emit a TEST/CORE node for a core type, else intern the extern
        and emit a TEST/TYPE node
        """

        core = CORE_TYPES.get(self._extern)
        if core is not None:
            return ctx.append_node(("TEST", "CORE", int(core)))
        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "TYPE", idx))


    def __repr__(self):
        return f'TYPE({self._extern!r})'


    def __kos_repr__(self):
        return f"(:: {self._extern})"


class PREDICATE(Pattern):
    """
    Match a single element for which the named extern callable returns
    a truthy value
    """


    def __init__(self, extern_name):
        """
        :param extern_name: the extern binding name supplied at match
            time
        :raises TypeError: when extern_name is not a str
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "PREDICATE extern name")
        self._extern = extern_name


    def lower(self, ctx):
        """
        Intern the extern and emit a TEST/PREDICATE node
        """

        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "PREDICATE", idx))


    def __repr__(self):
        return f'PREDICATE({self._extern!r})'


    def __kos_repr__(self):
        return f"(PREDICATE {self._extern})"


class CAPTURE(Pattern):
    """
    Bind one matched element (or a sub-pattern result) into a named slot
    """


    def __init__(self, slot, sub=None):
        """
        :param slot: target slot name
        :param sub: optional sub-pattern; when None the capture takes
            any single element
        :raises TypeError: when slot is not a str or sub is not a
            pattern or None
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "CAPTURE slot")
        if sub is not None:
            check_pattern_arg(sub, "CAPTURE sub")

        self._slot = slot
        self._sub = sub


    def _claim_signature(self):
        """
        Freeze-time tuple so repeated identical captures share a signature
        """

        if self._sub is None:
            return ("capture", None)
        if isinstance(self._sub, TYPE):
            return ("capture", ("type", self._sub._extern))
        return ("capture", ("complex", id(self._sub)))


    def lower(self, ctx):
        """
        Reserve the slot, lower the optional sub-pattern, and emit the
        CAPTURE node
        """

        ctx.claim_slot_name(self._slot, self._claim_signature())
        slot_idx = ctx.intern_slot(self._slot)
        if self._sub is None:
            sub_idx = -1
        else:
            sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("CAPTURE", slot_idx, sub_idx))


    def __repr__(self):
        parts = [f"CAPTURE({self._slot!r}"]
        if self._sub is not None:
            parts.append(f"{repr(self._sub)}")
        if len(parts) > 1:
            return ', '.join(parts) + ')'
        return parts[0] + ')'


    def __kos_repr__(self):
        sub = self._sub
        slot = self._slot
        if isinstance(sub, REPEAT):
            inner = sub._sub
            if (isinstance(inner, GROUP)
                    and inner._slot is None
                    and len(inner._children) == 1):
                inner = inner._children[0]
            parts = ['REPEAT', slot, kos_repr(inner)]
            if sub._min_count != 0:
                parts.extend(['min:', str(sub._min_count)])
            return f"({' '.join(parts)})"
        if isinstance(sub, REST):
            parts = ['REST', slot]
            if sub._min_tail != 0:
                parts.extend(['min:', str(sub._min_tail)])
            return f"({' '.join(parts)})"
        parts = [slot]
        if sub is None:
            if len(parts) == 1:
                return slot
            return f"({' '.join(parts)})"
        if isinstance(sub, TYPE):
            parts.extend(['::', sub._extern])
        elif isinstance(sub, LITERAL):
            parts.extend(['::', f"(LITERAL {kos_repr(sub._value)})"])
        else:
            parts.extend(['::', kos_repr(sub)])
        return f"({' '.join(parts)})"


class FORM(Pattern):
    """
    Descend into a single source element treated as a pair-tree and
    apply the inner pattern against its contents
    """


    def __init__(self, sub):
        """
        :param sub: pattern applied to the inner sequence
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "FORM sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then a FORM node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("FORM", sub_idx))


    def __repr__(self):
        return f"FORM({repr(self._sub)})"


    def __kos_repr__(self):
        return f"(FORM {kos_repr(self._sub)})"


class APP(Pattern):
    """
    Apply the named extern callable to a single source element and match
    the inner pattern against the call's result. The extern is invoked
    through the demand path at match time, so a plain koskari callable
    works directly; a genuine async suspension raises
    KoskariSyncViolation, and a raising extractor propagates as an
    error, not a no-match
    """


    def __init__(self, extern_name, sub=None):
        """
        :param extern_name: the extractor's extern binding name
        :param sub: optional pattern applied to the call result
        :raises TypeError: when extern_name is not a str or sub is not
            a pattern
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "APP extern name")
        if sub is not None:
            check_pattern_arg(sub, "APP sub")
        self._extern = extern_name
        self._sub = sub


    def lower(self, ctx):
        """
        Intern the extern, emit the inner pattern, then an APP node
        """

        idx = ctx.intern_extern(self._extern)
        sub_idx = self._sub.lower(ctx) if self._sub is not None else -1
        return ctx.append_node(("APP", idx, sub_idx))


    def __repr__(self):
        if self._sub is None:
            return f"APP({self._extern!r})"
        return f"APP({self._extern!r}, {repr(self._sub)})"


    def __kos_repr__(self):
        if self._sub is None:
            return f"(APP {self._extern})"
        return f"(APP {self._extern} {kos_repr(self._sub)})"


class MAP_ENTRY:
    """
    One mapping-pattern entry: a key plus an optional sub-pattern for
    the looked-up value. The key is a literal constant, or -- when
    ``extern`` is true -- an extern name resolved from the namespace at
    match time (the same channel as TYPE / CLASS names). Not a Pattern:
    only meaningful as a child of MAP
    """


    def __init__(self, key, sub=None, extern=False):
        """
        :param key: literal key value, or extern name str when extern
        :param sub: optional pattern applied to the looked-up value
        :param extern: when true, key is an extern binding name
        :raises TypeError: when sub is not a pattern, or extern is set
            and key is not a str
        """

        if extern:
            check_str_arg(key, "MAP_ENTRY extern key")
        if sub is not None:
            check_pattern_arg(sub, "MAP_ENTRY sub")
        self._key = key
        self._sub = sub
        self._extern = extern


    def lower(self, ctx):
        """
        Intern the key, emit the sub-pattern, then a MAP_ENTRY node
        """

        if self._extern:
            tag = "MAP_ENTRY_EXTERN"
            ref = ctx.intern_extern(self._key)
        else:
            tag = "MAP_ENTRY_CONST"
            ref = ctx.intern_const(self._key)
        sub_idx = self._sub.lower(ctx) if self._sub is not None else -1
        return ctx.append_node((tag, ref, sub_idx))


    def __repr__(self):
        parts = [repr(self._key)]
        if self._sub is not None:
            parts.append(repr(self._sub))
        if self._extern:
            parts.append("extern=True")
        return f"MAP_ENTRY({', '.join(parts)})"


class MAP(Pattern):
    """
    Match a single element as a mapping (dict or collections.abc
    Mapping): every entry's key must be present with a value matching
    the entry's sub-pattern. Extra keys are ignored, per PEP 634
    mapping-pattern semantics
    """


    def __init__(self, *entries):
        """
        :param entries: MAP_ENTRY children
        :raises TypeError: when an entry is not a MAP_ENTRY
        """

        for entry in entries:
            if not isinstance(entry, MAP_ENTRY):
                raise TypeError(
                    f"MAP entries must be MAP_ENTRY, got {entry!r}")
        self._entries = entries


    def lower(self, ctx):
        """
        Emit each entry then a MAP node chaining them
        """

        idxs = [entry.lower(ctx) for entry in self._entries]
        return ctx.append_node(("MAP",) + tuple(idxs))


    def __repr__(self):
        inner = ", ".join(repr(entry) for entry in self._entries)
        return f"MAP({inner})"


class MATCH(Pattern):
    """
    Sequential spine: match children in order in the current capture dict
    """


    def __init__(self, *children):
        """
        :param children: zero or more patterns
        :raises TypeError: when any child is not a pattern
        """

        for c in children:
            check_pattern_arg(c, "MATCH child")
        self._children = children


    def lower(self, ctx):
        """
        Emit each child then a MATCH node listing them in order
        """

        idxs = [c.lower(ctx) for c in self._children]
        return ctx.append_node(("MATCH",) + tuple(idxs))


    def __repr__(self):
        if not self._children:
            return "MATCH()"
        return f"MATCH({', '.join(map(repr, self._children))})"


    def __kos_repr__(self):
        if not self._children:
            return "(MATCH)"
        return f"(MATCH {' '.join(map(kos_repr, self._children))})"


class GROUP(Pattern):
    """
    Nested capture frame: inner captures go into a sub-dict. The
    GROUP node has no slot; when ``slot`` is a non-empty str this pattern
    lowers to ``CAPTURE(slot, GROUP(children...))`` so naming stays on
    CAPTURE only. When ``slot`` is None the sub-dict is matched and then
    discarded unless a parent CAPTURE or REPEAT row collects it
    """


    def __init__(self, slot, *children):
        """
        :param slot: dict key for the nested result when lowering wraps
            CAPTURE, or None for an anonymous group
        :param children: zero or more patterns matched in order inside
            the nested dict
        """

        if slot is not None:
            check_str_arg(slot, "GROUP slot")
        for c in children:
            check_pattern_arg(c, "GROUP child")
        self._slot = slot
        self._children = children


    def lower(self, ctx):
        """
        Emit a slot-free GROUP node; when named, wrap with CAPTURE so the
        slot is claimed only on the outer CAPTURE
        """

        if self._slot is not None:
            ctx.claim_slot_name(self._slot, ("group", id(self)))
            slot_idx = ctx.intern_slot(self._slot)
        ctx.begin_nested_claims(inherit_formals=False)
        try:
            idxs = [c.lower(ctx) for c in self._children]
        finally:
            ctx.end_nested_claims()
        g_idx = ctx.append_node(("GROUP",) + tuple(idxs))
        if self._slot is None:
            return g_idx
        return ctx.append_node(("CAPTURE", slot_idx, g_idx))


    def __repr__(self):
        slot = self._slot
        if self._children:
            ch = ', '.join(map(repr, self._children))
            return f"GROUP({slot}, {ch})"
        return f"GROUP({slot})"


    def __kos_repr__(self):
        head = 'nil' if self._slot is None else self._slot
        return f"(GROUP {head} {' '.join(map(kos_repr, self._children))})"


class ONEOF(Pattern):
    """
    Disjunction: try each branch in order until one matches
    """


    def __init__(self, *branches):
        """
        :param branches: one or more patterns
        :raises TypeError: when any branch is not a pattern
        :raises ValueError: when called with no branches
        """

        if not branches:
            raise ValueError("ONEOF requires at least one branch")
        for b in branches:
            check_pattern_arg(b, "ONEOF branch")
        self._branches = branches


    def lower(self, ctx):
        """
        Emit each branch then a ONEOF node listing them in order. Each
        branch gets its own capture scope so identical slot names across
        mutually exclusive arms remain valid
        """

        idxs = []
        alternatives = {}
        for b in self._branches:
            ctx.begin_nested_claims(alternatives=alternatives)
            try:
                idxs.append(b.lower(ctx))
            finally:
                ctx.end_nested_claims()
        return ctx.append_node(("ONEOF",) + tuple(idxs))


    def __repr__(self):
        return f"ONEOF({', '.join(map(repr, self._branches))})"


    def __kos_repr__(self):
        return f"(ONEOF {' '.join(map(kos_repr, self._branches))})"


class REPEAT(Pattern):
    """
    Greedily match the inner pattern repeatedly; require at least
    ``min_count`` successful iterations (default 0: Kleene star)
    """


    def __init__(self, sub, min_count=0):
        """
        :param sub: pattern repeated against successive elements
        :param min_count: minimum iterations (0 allows zero matches
            when wrapped in CAPTURE / repeat_capture)
        :raises TypeError: when sub is not a pattern or min_count is
            not an int
        :raises ValueError: when min_count is negative
        """

        check_pattern_arg(sub, "REPEAT sub")
        if isinstance(min_count, bool) or not isinstance(min_count, int):
            raise TypeError(
                f"REPEAT min_count must be an int, got {type(min_count).__name__!r}")
        if min_count < 0:
            raise ValueError(
                f"REPEAT min_count must be non-negative, got {min_count}")
        self._sub = sub
        self._min_count = min_count


    def lower(self, ctx):
        """
        Emit the inner pattern then a REPEAT node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("REPEAT", sub_idx, self._min_count))


    def __repr__(self):
        if self._min_count == 0:
            return f"REPEAT({repr(self._sub)})"
        return f"REPEAT({repr(self._sub)}, min_count={self._min_count})"


    def __kos_repr__(self):
        if self._min_count == 0:
            return f"(REPEAT {kos_repr(self._sub)})"
        return f"(REPEAT {kos_repr(self._sub)} min: {self._min_count})"


class OPTIONAL(Pattern):
    """
    Match the inner pattern at most once. When the inner pattern does
    not match, any capture slots it would have bound are omitted from
    the result captures dict
    """


    def __init__(self, sub):
        """
        :param sub: pattern that may or may not match
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "OPTIONAL sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then an OPTIONAL node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("OPTIONAL", sub_idx))


    def __repr__(self):
        return f"OPTIONAL({repr(self._sub)})"


    def __kos_repr__(self):
        return f"(OPTIONAL {kos_repr(self._sub)})"


class REST(Pattern):
    """
    Consume the remainder of the current spine subject. ``min_tail`` is
    the minimum number of spine cells this REST must take (default 0).
    When ``slot`` is set, lowering emits ``CAPTURE(slot, REST(...))`` so
    only CAPTURE carries a slot index on the wire
    """


    def __init__(self, slot=None, min_tail=0):
        """
        :param slot: optional capture slot name for the tail value
        :param min_tail: minimum spine elements consumed (default 0)
        :raises TypeError: when slot is not None and not str, or
            min_tail is not an int
        :raises ValueError: when slot is empty or min_tail is negative
        """

        if slot is not None:
            check_str_arg(slot, "REST slot")
        if isinstance(min_tail, bool) or not isinstance(min_tail, int):
            raise TypeError(
                f"REST min_tail must be an int, got {type(min_tail).__name__!r}")
        if min_tail < 0:
            raise ValueError(
                f"REST min_tail must be non-negative, got {min_tail}")
        self._slot = slot
        self._min_tail = min_tail


    def lower(self, ctx):
        """
        Emit REST with min_tail only; when named, wrap with CAPTURE
        """

        if self._slot is not None:
            ctx.claim_slot_name(self._slot, ("rest", id(self)))
            slot_idx = ctx.intern_slot(self._slot)
        rest_idx = ctx.append_node(("REST", self._min_tail))
        if self._slot is None:
            return rest_idx
        return ctx.append_node(("CAPTURE", slot_idx, rest_idx))


    def __repr__(self):
        if self._slot is None and self._min_tail == 0:
            return "REST()"
        if self._slot is None:
            return f"REST(min_tail={self._min_tail})"
        if self._min_tail == 0:
            return f"REST({self._slot})"
        return f"REST({self._slot}, min_tail={self._min_tail})"


    def __kos_repr__(self):
        if self._slot is None:
            if self._min_tail == 0:
                return "(REST)"
            return f"(REST min: {self._min_tail})"
        if self._min_tail != 0:
            return f"(REST {self._slot} min: {self._min_tail})"
        return f"(REST {self._slot})"


class NOT(Pattern):
    """
    Negative lookahead: succeed when the inner pattern does not match
    the current single element. Width 1 with no captures inside
    """


    def __init__(self, sub):
        """
        :param sub: pattern whose match would cause NOT to fail
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "NOT sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then a NOT node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("NOT", sub_idx))


    def __repr__(self):
        return f"NOT({repr(self._sub)})"


    def __kos_repr__(self):
        return f"(NOT {kos_repr(self._sub)})"


class CLASS(Pattern):
    """
    Match a single element which is an instance of the named extern
    type. Field children project named or positional attributes through
    sub-patterns
    """


    def __init__(self, type_extern, *fields):
        """
        :param type_extern: name of the extern binding for the class
            type
        :param fields: zero or more FIELD instances
        :raises TypeError: when type_extern is not a str or a field is
            not a FIELD
        :raises ValueError: when type_extern is empty
        """

        check_str_arg(type_extern, "CLASS type_extern")
        for f in fields:
            if not isinstance(f, FIELD):
                raise TypeError(
                    "CLASS field arguments must be FIELD instances, "
                    f"got {f!r}")
        self._extern = type_extern
        self._fields = fields


    def lower(self, ctx):
        """
        Emit each FIELD child then a CLASS node referencing them.
        Toggles ctx.in_class_fields so that nested CLASS/FIELD trees
        validate correctly. The CLASS extern is interned before
        recursing into fields so that the externs tuple reflects
        source declaration order
        """

        ext_idx = ctx.intern_extern(self._extern)
        prev = ctx.in_class_fields
        ctx.in_class_fields = True
        try:
            field_idxs = [f.lower(ctx) for f in self._fields]
        finally:
            ctx.in_class_fields = prev
        return ctx.append_node(
            ("CLASS", ext_idx) + tuple(field_idxs))


    def __repr__(self):
        if not self._fields:
            return f"CLASS({self._extern!r})"
        return f"CLASS({self._extern!r}, {', '.join(map(repr, self._fields))})"


    def __kos_repr__(self):
        if not self._fields:
            return f"(CLASS {self._extern})"
        return f"(CLASS {self._extern} {' '.join(map(kos_repr, self._fields))})"


class FIELD(Pattern):
    """
    Project a single attribute of a CLASS instance through an inner
    pattern. Positional when name is None, keyword otherwise
    """


    def __init__(self, sub, *, name=None):
        """
        :param sub: pattern applied to the projected attribute value
        :param name: optional attribute name; when None the field is
            positional and resolved via __match_args__
        :raises TypeError: when sub is not a pattern or name is not a
            str
        :raises ValueError: when name is provided but empty
        """

        check_pattern_arg(sub, "FIELD sub")
        if name is not None:
            check_str_arg(name, "FIELD name")
        self._sub = sub
        self._name = name


    def lower(self, ctx):
        """
        Emit a FIELD_POS or FIELD_KW node. Rejects emission outside a
        CLASS scope. Temporarily clears in_class_fields so that the
        sub-pattern can host its own CLASS/FIELD nesting
        """

        if not ctx.in_class_fields:
            raise ValueError(
                "FIELD must be a child of CLASS")

        prev = ctx.in_class_fields
        ctx.in_class_fields = False
        try:
            sub_idx = self._sub.lower(ctx)
        finally:
            ctx.in_class_fields = prev

        if self._name is None:
            return ctx.append_node(("FIELD_POS", sub_idx))
        return ctx.append_node(("FIELD_KW", sub_idx, self._name))


    def __repr__(self):
        if self._name is None:
            return f"FIELD({repr(self._sub)})"
        return f"FIELD({repr(self._sub)}, name={self._name!r})"


    def __kos_repr__(self):
        if self._name is None:
            return f"(FIELD {kos_repr(self._sub)})"
        return f"(FIELD {kos_repr(self._sub)} {self._name})"


def repeat_capture(slot, inner):
    """
    Capture the results of repeating inner across successive subject
    elements. captures[slot] is a Python list: each element is one
    iteration result (often a dict for a GROUP row dict, or a sparse
    change dict / Datum when inner does not use that shape)

    :param slot: outer slot receiving the list of per-iteration values
    :param inner: pattern run once per repetition (often FORM around
        a MATCH of captures)
    :rtype: CAPTURE
    """

    check_str_arg(slot, "repeat_capture slot")
    check_pattern_arg(inner, "repeat_capture inner")
    if isinstance(inner, REPEAT):
        sub = inner._sub
        mc = inner._min_count
        return CAPTURE(
            slot,
            REPEAT(GROUP(None, sub), min_count=mc))
    return CAPTURE(slot, REPEAT(GROUP(None, inner)))


def binding_inner_pattern():
    """
    Build the inner shape of a single koskari binding cell as matcher
    primitives: FORM around ONEOF of (SYM EXPR) or (EXPR as: SYM).
    The outer FORM descends into one spine element that is already a
    binding pair (SYM EXPR). For a grouped cell such as ``((t 1))`` from
    ``[[t 1]]``, use :func:`bindings_grouped_list_cell_pattern` inside
    :class:`BINDINGS` instead; a single FORM here would yield an inner
    spine of one pair element, and ONEOF would run MATCH(SYMBOL, ...) on
    that pair and fail

    Does not wrap an outer CAPTURE; when embedding multiple binding
    cells in one matcher without duplicate-slot errors, use distinct
    capture slot names, :class:`ONEOF` per-branch capture scopes, or
    separate :class:`GROUP` arms

    :rtype: FORM
    """

    return FORM(ONEOF(
        MATCH(SYMBOL("name"), VALUE("expr")),
        MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    ))


def bindings_grouped_list_cell_pattern():
    """
    Match one binding-list element that wraps a single pair in an extra
    list cell, e.g. ``((t 1))`` from ``[[t 1]]``. FORM enters the outer
    list; MATCH consumes the sole inner element; inner FORM enters the
    pair tree for ONEOF(MATCH, MATCH)
    """

    inner = binding_inner_pattern()
    return FORM(MATCH(inner))


# === Ergonomic shorthands ===


def BIND(slot):
    """
    Capture any single element into the named slot
    """

    return CAPTURE(slot)


def SYMBOL(slot):
    """
    Capture a symbol element into the named slot. Requires the
    'symbol' extern at match time
    """

    return CAPTURE(slot, TYPE("symbol"))


def KEYWORD(slot):
    """
    Capture a keyword element into the named slot. Requires the
    'keyword' extern at match time
    """

    return CAPTURE(slot, TYPE("keyword"))


def PAIR(slot):
    """
    Capture a pair element into the named slot. Requires the 'pair'
    extern at match time
    """

    return CAPTURE(slot, TYPE("pair"))


def LITERAL_KW(s):
    """
    Match a keyword equal to keyword(s)
    """

    return LITERAL(keyword(s))


def LITERAL_SYM(s):
    """
    Match a symbol equal to symbol(s)
    """

    return LITERAL(symbol(s))


def LIST(*specs, key=None):
    """
    Match a single source element which is a list whose contents
    satisfy the given specs in order. When key is given the matched
    sub-list is also captured into that slot
    """

    inner = FORM(MATCH(*specs))
    if key is None:
        return inner
    return CAPTURE(key, inner)


# === One-shot matching ===


def match_pattern_raw(source, patterns):
    """
    Try each matcher pattern against ``source`` (pair spine or ``nil``).
    Return the captures of the first match, else ``None``
    """

    if not is_nil(source) and not is_pair(source):
        raise TypeError(
            f"match source must be nil or a pair, got {source!r}")

    if not isinstance(patterns, (list, tuple)):
        patterns = [patterns]

    for pat in patterns:
        if not isinstance(pat, Pattern):
            raise TypeError(
                f"match_pattern_raw expects a matcher Pattern, got {pat!r}")
        captures = demand(pat.build())(source)
        if captures is not None:
            return captures

    return None


def match(source, patterns):
    """
    Try each matcher pattern against ``source`` (pair spine or ``nil``).
    Returns the captures of the first match, else ``None``.
    """

    return match_pattern_raw(source, patterns)


def match1(value, patterns):
    """
    Match a single element by wrapping ``cons(value, nil)``
    """

    return match(cons(value, nil), patterns)


def make_matcher(pattern, externs=None):
    """
    Compile a pattern AST to its matcher function, as
    :meth:`Pattern.build` does

    :param pattern: a matcher pattern object
    :param externs: the extern names mapped to their values, or None
    :raises TypeError: when pattern is not a matcher pattern
    :rtype: Function
    """

    if isinstance(pattern, (pair, symbol)):
        pattern = parse_matcher(pattern)

    if not isinstance(pattern, Pattern):
        raise TypeError(
            f"make_matcher requires a matcher pattern, got {type(pattern).__name__!r}")

    return pattern.build(externs)


# === Composite matchers ===


class Composite(Pattern):
    """
    Base class for matcher composite matchers. Subclasses return
    a tree of primitive Pattern nodes from _expand; lower on the
    composite lowers that tree. Composites contribute no match-time Python
    behaviour: every semantic concern is encoded structurally
    """


    def lower(self, ctx):
        """
        Emit the expanded primitive pattern
        """

        primitive = self._expand()
        return primitive.lower(ctx)


    def _expand(self):
        """
        Subclass responsibility: return the primitive Pattern tree
        that performs the structural match for this composite
        """

        ...


class CALLEDBY(Composite):
    """
    Capture the compile-head symbol into the called-by slot
    """

    def __init__(self, slot=None):
        """
        :param slot: target slot name
        """

        self._slot = slot


    def _expand(self):
        slot = self._slot or "called-by"
        return CAPTURE(slot, TYPE("symbol"))


    def __repr__(self):
        if self._slot is None:
            return "CALLEDBY()"
        return f"CALLEDBY({self._slot!r})"


    def __kos_repr__(self):
        if self._slot is None:
            return "(CALLEDBY)"
        return f"(CALLEDBY {self._slot!r})"


class DEFINE(Composite):
    """
    Capture one element constrained to be a symbol. Used as the
    introduced-name slot for define-shaped specials
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "DEFINE slot")
        self._slot = slot


    def _expand(self):
        return CAPTURE(self._slot, TYPE("symbol"))


    def __repr__(self):
        return f'DEFINE({self._slot!r})'


    def __kos_repr__(self):
        return f'(DEFINE {self._slot})'


class DECL(Composite):
    """
    Capture one element shaped as a declaration: a bare symbol, a
    one-element list naming the symbol, the (type as: name) form,
    or a (name . tail) form where tail captures the keyword
    attribute pairs verbatim. After a successful match captures[slot]
    is a dict (the GROUP's captures) or :class:`DatumFrame` for KosPair-backed
    subjects. Keys include ``name`` where applicable, ``type`` from the
    as-form, ``decl_tail`` from the dotted tail arm; other keys are omitted.
    The whole declaration cell spine is available as ``.source`` on
    :class:`DatumFrame` results (not a dict key)
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "DECL slot")
        self._slot = slot


    def __repr__(self):
        return f'DECL({self._slot!r})'


    def __kos_repr__(self):
        return f'(DECL {self._slot})'


    def _expand(self):
        return GROUP(self._slot, ONEOF(
            TYPE("symbol"),
            FORM(MATCH(SYMBOL("name"))),
            FORM(MATCH(
                VALUE("type"),
                LITERAL_KW("as"),
                SYMBOL("name"))),
            FORM(MATCH(SYMBOL("name"), REST("decl_tail"))),
        ))


class BINDING(Composite):
    """
    Capture a single binding-shaped cell, (NAME EXPR) or
    (EXPR as: NAME). After a successful match captures[slot] is a dict
    (the GROUP's captures) or :class:`DatumFrame` with ``name`` and ``expr`` as
    produced by binding_inner_pattern. For KosPair-backed subjects the
    whole binding cell spine is ``.source`` on :class:`DatumFrame`
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "BINDING slot")
        self._slot = slot


    def _expand(self):
        return GROUP(self._slot, binding_inner_pattern())


    def __repr__(self):
        return f'BINDING({self._slot!r})'


    def __kos_repr__(self):
        return f'(BINDING {self._slot})'


class BINDINGS(Composite):
    """
    Capture a binding-list cell: a single element which is either nil
    or a proper list of binding-shaped cells. The nonempty arm uses
    repeat_capture so captures[slot] is a bare Python list of per-cell
    frame dicts (same keys as :class:`BINDING`: ``name``, ``expr``), not
    Datum-wrapped; the empty arm matches nil and binds
    captures[slot] = nil (a Datum in the usual CAPTURE sense). The two
    arms share an outer slot name through ONEOF's per-branch capture
    scoping
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "BINDINGS slot")
        self._slot = slot


    def __repr__(self):
        return f'BINDINGS({self._slot!r})'


    def __kos_repr__(self):
        return f'(BINDINGS {self._slot})'


    def _expand(self):
        """
        Match one subject cell (``nil`` or a proper binding list). The repeat
        must run inside that cell via ``FORM`` so we do not treat following
        special-form siblings (e.g. let body) as extra binding rows.
        """

        row = GROUP(None, ONEOF(
            bindings_grouped_list_cell_pattern(),
            binding_inner_pattern(),
        ))
        return ONEOF(
            CAPTURE(self._slot, LITERAL(nil)),
            FORM(CAPTURE(self._slot, REPEAT(row))),
        )


def formal_param_pattern():
    """
    Build a one-element matcher that recognises a single formal
    parameter cell. Accepted shapes: bare symbol, (name), (name default), (type as: name),
    (name kw1 v1 kw2 v2 ...) with one-or-more keyword/value pairs
    in the tail, and (name: default kw1 v1 ...) where a keyword heads
    the cell and its value is the default. Captures inside this
    pattern appear in the enclosing REPEAT frame as 'name' (or 'kw'
    for the keyword-headed shape), 'default', 'type', and 'attrs' (a
    list of {'kw', 'attr_val'} frame dicts from CAPTURE/VALUE pairs)
    depending on which arm fires
    """

    attrs = repeat_capture("attrs", MATCH(
        CAPTURE("kw", TYPE("keyword")),
        VALUE("attr_val")))

    return ONEOF(
        SYMBOL("name"),
        FORM(MATCH(SYMBOL("name"))),
        FORM(MATCH(
            LITERAL_SYM("ref"),
            SYMBOL("ref_name"))),
        FORM(MATCH(SYMBOL("name"), VALUE("default"))),
        FORM(MATCH(VALUE("type"), LITERAL_KW("as"), SYMBOL("name"))),
        FORM(MATCH(SYMBOL("name"), attrs)),
        FORM(MATCH(
            CAPTURE("kw", TYPE("keyword")),
            VALUE("default"),
            OPTIONAL(attrs))),
    )


def frame_formal_name(fr):
    """
    Resolve one positional formal frame to (name, by_ref).

    The binding symbol lives under 'ref_name' for the (ref x) and
    (ref x) by-ref shapes. Otherwise it lives under 'name'; a 'name' carrying
    a leading & is the reader-disabled by-ref fallback and is stripped. A
    keyword-headed cell such as (y: 1 :: int) names the parameter under
    'kw' and the symbol that keyword spells is returned; a leading & on
    the keyword marks it by-ref. The returned name is the raw symbol
    (Datum-unwrapped) or None
    """

    v = fr.get("ref_name")
    if v is not None:
        n = v.datum if isinstance(v, Datum) else v
        return n, True

    v = fr.get("name")
    if v is not None:
        n = v.datum if isinstance(v, Datum) else v
        if is_symbol(n) and str(n)[:1] == "&":
            return symbol(str(n)[1:]), True
        return n, False

    v = fr.get("kw")
    if v is None:
        return None, False
    kw = v.datum if isinstance(v, Datum) else v
    if not is_keyword(kw):
        return None, False
    s = str(kw)
    if s[:1] == "&":
        return symbol(s[1:]), True
    return symbol(s), False


def kwonly_param_pattern():
    """
    Build a one-element matcher that recognises a single kw-only
    parameter cell. After the *: marker each kw-only entry is either
    a bare symbol (required keyword argument) or a keyword followed
    by a value (defaulted keyword argument). The leading **: marker
    is excluded with a NOT lookahead so the kw-only REPEAT terminates
    cleanly before the optional **: tail
    """

    return MATCH(
        NOT(LITERAL_KW("**")),
        ONEOF(
            FORM(MATCH(SYMBOL("name"))),
            MATCH(SYMBOL("name")),
            MATCH(CAPTURE("kw", TYPE("keyword")), VALUE("default")),
        ),
    )


def default_pair_pattern():
    """
    Build the matcher for one positional-default keyword/value pair
    appearing after the positional run and before *: or **:. The
    leading lookaheads exclude the *: and **: markers so the REPEAT
    of default pairs stops at the first marker keyword
    """

    return MATCH(
        NOT(LITERAL_KW("*")),
        NOT(LITERAL_KW("**")),
        CAPTURE("kw", TYPE("keyword")),
        VALUE("default"),
    )


def formals_inner_pattern():
    """
    Build the proper-list arm of the FORMALS matcher. The list-shape
    decomposes into five optional phases consumed in order:

    * a positional run capturing each formal as a REPEAT frame
    * a default keyword/value run capturing each pair as a REPEAT
      frame (frames hold 'kw' and 'default')
    * an optional *: segment binding either a star symbol or nil
    * a kw-only run capturing each entry as a REPEAT frame (frames
      hold 'name' for bare symbols or 'kw' and 'default' for
      keyword/value pairs)
    * an optional **: segment binding the starstar symbol

    No phase is required; an empty proper list matches with optional
    phases contributing no capture keys
    """

    star_segment = MATCH(
        LITERAL_KW("*"),
        ONEOF(SYMBOL("star"), CAPTURE("star", LITERAL(nil))),
    )

    starstar_segment = MATCH(
        LITERAL_KW("**"),
        SYMBOL("starstar"),
    )

    return FORM(MATCH(
        OPTIONAL(repeat_capture("positionals", formal_param_pattern())),
        OPTIONAL(repeat_capture("defaults", default_pair_pattern())),
        OPTIONAL(star_segment),
        OPTIONAL(repeat_capture("kwonly", kwonly_param_pattern())),
        OPTIONAL(starstar_segment),
    ))


class FORMALS(Composite):
    """
    Capture one element shaped as a proper-list formals cell and
    decompose its structure into a nested capture dict. Improper-list
    formals are rejected because koskari does not allow them as a
    function-formals shape. After a successful match captures[slot] is
    a dict (the GROUP's captures) or :class:`DatumFrame` whose keys include:

    * 'positionals': bare Python list of frame dicts (one per positional formal)
    * 'defaults': bare Python list of frame dicts (one per default keyword/value)
    * 'star': the symbol after *: (or nil for the *: nil marker)
    * 'kwonly': bare Python list of frame dicts (one per kw-only entry)
    * 'starstar': the symbol after **:

    Inner keys for phases that did not run are omitted from the group
    dict. For KosPair-backed subjects the whole formals list spine is
    ``.source`` on :class:`DatumFrame`. Use one FORMALS per matcher
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "FORMALS slot")
        self._slot = slot


    def _expand(self):
        return GROUP(self._slot, formals_inner_pattern())


    def __repr__(self):
        return f'FORMALS({self._slot!r})'


    def __kos_repr__(self):
        return f'(FORMALS {self._slot})'


class USING(Composite):
    """
    Capture one element shaped as a using-form. Four inner shapes
    are recognised through an ordered ONEOF; first match wins:

    1. ((PATTERN...) EXPR) -- unpack form, captures lhs and expr
    2. (NAME EXPR)         -- named binding, captures name and expr
    3. (EXPR as: NAME)     -- as form, captures expr and name
    4. (EXPR)              -- bare expression, captures expr only

    After a successful match captures[slot] is a dict (the GROUP's captures) or
    :class:`DatumFrame` with whichever of ``lhs``, ``name``, ``expr`` the
    winning arm binds. For KosPair-backed subjects the whole cell spine is
    ``.source`` on :class:`DatumFrame`
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "USING slot")
        self._slot = slot


    def __repr__(self):
        return f'USING({self._slot!r})'


    def __kos_repr__(self):
        return f'(USING {self._slot})'


    def _expand(self):
        return GROUP(self._slot, FORM(ONEOF(
            MATCH(PAIR("lhs"), VALUE("expr")),
            MATCH(SYMBOL("name"), VALUE("expr")),
            MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
            MATCH(VALUE("expr")),
        )))


class USING_UNPACK(Composite):
    """
    Capture ``((PATTERN...) EXPR)`` for unpack bindings (e.g. for-each).
    After a successful match captures[slot] is a dict (the GROUP's captures) or
    :class:`DatumFrame` with ``lhs`` and ``expr``. For KosPair-backed
    subjects the whole cell spine is ``.source`` on :class:`DatumFrame`
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "USING_UNPACK slot")
        self._slot = slot


    def __repr__(self):
        return f'USING_UNPACK({self._slot!r})'


    def __kos_repr__(self):
        return f'(USING_UNPACK {self._slot})'


    def _expand(self):
        return GROUP(self._slot, FORM(MATCH(PAIR("lhs"), VALUE("expr"))))


class VALUE(Pattern):
    """
    Single-element capture of a slot holding an expression the form
    evaluates. Lowers to the same ``CAPTURE`` node as
    ``BIND(slot)``; the spelling records the slot's role
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "VALUE slot")
        self._slot = slot


    def lower(self, ctx):
        """
        Emit ``CAPTURE``
        """

        return CAPTURE(self._slot).lower(ctx)


    def __repr__(self):
        return f'VALUE({self._slot!r})'


    def __kos_repr__(self):
        return f'(VALUE {self._slot})'


class BODY(Pattern):
    """
    Remainder capture of a slot holding the forms a body evaluates.
    Lowers to the same ``REST`` node as ``REST(slot, min_tail=1)``
    (minimum one spine cell in the body tail); the spelling records
    the slot's role
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "BODY slot")
        self._slot = slot


    def lower(self, ctx):
        """
        Emit ``REST`` with ``min_tail=1``
        """

        return REST(self._slot, min_tail=1).lower(ctx)


    def __repr__(self):
        return f"BODY({self._slot!r})"


    def __kos_repr__(self):
        return f"(BODY {self._slot})"


# === The binding cell forms ===


# the shapes of one binding cell, (SYM EXPR) or (EXPR as: SYM), and of
# one using cell, which also admits a bare (EXPR)
BINDING_FORMS = (
    LIST(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    LIST(SYMBOL("name"), VALUE("expr")),
)

USING_FORMS = (
    *BINDING_FORMS,
    LIST(VALUE("expr")),
)


# === Self-describing matcher syntax: registry, meta-matcher, parser ===


MATCHER_REGISTRY = {}


def register_matcher(name, factory):
    """
    Register a factory for the surface head name. Raises ValueError on
    collision unless the existing entry is the same callable
    (idempotent re-registration is permitted)

    :param name: head atom string (the symbol name appearing in
        surface position, e.g. 'FORM' or 'BINDING')
    :param factory: callable invoked as factory(head_sym, raw_args)
        returning a matcher pattern object; raw_args is the
        koskari pair-list tail of the surface form
    :raises TypeError: when name is not a str or factory is not
        callable
    :raises ValueError: when name is empty or already registered to
        a different callable
    """

    if not isinstance(name, str):
        raise TypeError(
            f"register_matcher name must be a str, got {name!r}")
    if not name:
        raise ValueError("register_matcher name must be non-empty")
    if not callable(factory):
        raise TypeError(
            f"register_matcher factory must be callable, got {factory!r}")
    existing = MATCHER_REGISTRY.get(name)
    if existing is not None and existing is not factory:
        raise ValueError(
            f"matcher {name!r} already registered to a different factory")
    MATCHER_REGISTRY[name] = factory


def lookup_matcher(name):
    """
    Return the registered factory for name, or None when no factory
    has been registered under that name

    :param name: head atom string
    :rtype: callable or None
    """

    return MATCHER_REGISTRY.get(name)


KW_DCOLON = keyword("::")
KW_TYPE = keyword("type")
KW_VAL = keyword("val")
KW_STAR = keyword("*")


META_PATTERN = ONEOF(
    FORM(MATCH(
        LITERAL_SYM("quote"),
        CAPTURE("quoted_symbol", TYPE("symbol")))),
    FORM(MATCH(
        LITERAL_KW("::"),
        REST("dcolon_args"))),
    FORM(MATCH(
        LITERAL_KW("type"),
        REST("type_args"))),
    FORM(MATCH(
        LITERAL_KW("val"),
        CAPTURE("val_expr"))),
    FORM(MATCH(
        LITERAL_SYM("CLASS"),
        CAPTURE("class_extern", TYPE("symbol")),
        REST("class_fields"))),
    FORM(MATCH(
        CAPTURE("head", TYPE("symbol")),
        REST("head_args"))),
    CAPTURE("bare_symbol", TYPE("symbol")),
    CAPTURE("atom_literal"),
)


# the matcher functions the parsers run from Python, each wrapped with
# demand once
META_MATCHER = demand(make_matcher(MATCH(META_PATTERN)))


def parse_matcher(elem):
    """
    Lower one matcher-syntax element to a matcher pattern object.
    Runs META_MATCHER against (elem,), then dispatches on which
    capture slot is bound. Raises ValueError when elem does not
    satisfy any known surface form

    :param elem: one koskari source element (atom or pair-tree)
    :rtype: Pattern
    """

    if is_nil(elem):
        raise ValueError(
            "nil is not a valid matcher pattern element")
    # the element is source, so it is walked as a one-cell spine:
    # captures come back as Datum with position intact
    c = META_MATCHER(cons(elem, nil))
    if c is None:
        raise ValueError(
            f"not a valid matcher pattern: {elem!r}")

    if "bare_symbol" in c:
        return CAPTURE(str(c["bare_symbol"].datum))

    if "atom_literal" in c:
        atom = c["atom_literal"].datum
        if isinstance(atom, pair):
            raise ValueError(
                f"improper list is not a valid matcher pattern: {atom!r}")
        return LITERAL(atom)

    if "quoted_symbol" in c:
        return LITERAL_SYM(str(c["quoted_symbol"].datum.base))

    if "dcolon_args" in c:
        return lower_anon_constraint(
            "::", c["dcolon_args"].datum, elem)

    if "type_args" in c:
        return lower_anon_constraint(
            "type:", c["type_args"].datum, elem)

    if "val_expr" in c:
        return LITERAL(c["val_expr"].datum)

    if "class_extern" in c:
        fields = (
            c["class_fields"].datum if "class_fields" in c else nil)
        return lower_class(c["class_extern"].datum, fields)

    if "head" in c:
        head = c["head"].datum
        args = c["head_args"].datum if "head_args" in c else nil
        name = str(head)
        factory = MATCHER_REGISTRY.get(name)
        if factory is not None:
            return factory(head, args)
        return lower_named_capture(head, args)

    raise ValueError(
        f"internal: meta-matcher matched {elem!r} with no recognised slot bound")


def parse_explicit_rule_pattern(form, source_ctx=None):
    """
    Lower MATCH-dialect pattern source (a cons tree) to a matcher Pattern.

    Distinct from :func:`parse_matcher`, which lowers a single META element.
    This walks a rule-level spine: either a registered declaration
    ``(HEAD slot . opts)`` when ``HEAD`` is in :data:`MATCHER_REGISTRY` and
    ``slot`` is a symbol, or a sequential ``MATCH`` of per-cell patterns.

    :param form: koskari pattern datum
    :param source_ctx: optional value appended to error messages
    :return: Pattern instance (often ``MATCH`` wrapping a sequence)
    :raises ValueError: on ellipsis, improper lists, empty cells, or
        factory failures
    """

    ctx = "" if source_ctx is None else f" (context {source_ctx!r})"

    if form is Ellipsis:
        raise ValueError(f"invalid ellipsis in MATCH pattern{ctx}")

    if not is_pair(form):
        return LITERAL(form)

    if not is_proper(form):
        raise ValueError(f"MATCH pattern must be a proper list{ctx}")

    # nil is a pair and a proper list; as a cell it would be an empty
    # inline group, which matches nothing and names nothing
    if is_nil(form):
        raise ValueError(f"empty MATCH pattern cell{ctx}")

    head, tail = form

    # a pattern a template wrote arrives marked: the head names a
    # registered matcher by its bare spelling, and the slot keeps its
    # mark so the template's marked reference finds it
    if not is_nil(tail) and is_pair(tail):
        decl_bind = tail.car
        factory = lookup_matcher(str(strip_mark(head)))
        if factory is not None and is_symbol(decl_bind):
            return factory(head, tail)

    specs = []
    cur = form
    while not is_nil(cur):
        first, cur = cur
        specs.append(parse_explicit_rule_pattern(first, source_ctx))
    return MATCH(*specs)


def lower_anon_constraint(tag, args, source):
    """
    Lower (:: pat) or (type: pat) or (val: pat) anonymous-constraint
    forms. For :: and type:, a single symbol payload becomes
    TYPE(name); a single pair payload recurses via parse_matcher.
    For val:, the single payload is wrapped in LITERAL verbatim
    """

    items = list(unpack(args))
    if len(items) != 1:
        raise ValueError(
            f"({tag}) expects exactly one payload, got {len(items)} in {source!r}")
    payload = strip_mark(items[0])
    if tag == "val:":
        return LITERAL(payload)
    if is_symbol(payload):
        return TYPE(str(payload))
    return parse_matcher(payload)


def lower_class(extern_sym, fields):
    """
    Lower (CLASS Sym field-pat...) into CLASS(name, *field-objs).
    Each field element must lower (via parse_matcher) to a FIELD
    instance; non-FIELD lowerings are rejected
    """

    type_name = str(extern_sym)
    field_objs = []
    for f in unpack(fields):
        lowered = parse_matcher(f)
        if not isinstance(lowered, FIELD):
            raise ValueError(
                f"(CLASS {type_name} ...) field elements must lower to FIELD,"
                f" got {lowered!r}")
        field_objs.append(lowered)
    return CLASS(type_name, *field_objs)


def lower_named_capture(name_sym, args):
    """
    Lower (NAME args...) where NAME is not a registered head. Walks
    args for constraints. Inline ::, type:, and val: markers are
    recognised inside the args list as constraint shorthand. The
    returned CAPTURE accepts at most one constraint child; multiple
    constraints raise ValueError
    """

    name = str(name_sym)
    constraints = []
    items = list(unpack(args))
    i = 0
    n = len(items)
    while i < n:
        elem = strip_mark(items[i])
        if is_keyword(elem):
            if elem is KW_DCOLON or elem is KW_TYPE:
                if i + 1 >= n:
                    raise ValueError(
                        f"({name} ...) constraint marker missing payload")
                payload = strip_mark(items[i + 1])
                if is_symbol(payload):
                    constraints.append(TYPE(str(payload)))
                else:
                    constraints.append(parse_matcher(payload))
                i += 2
                continue
            if elem is KW_VAL:
                if i + 1 >= n:
                    raise ValueError(
                        f"({name} ...) val: marker missing payload")
                constraints.append(LITERAL(items[i + 1]))
                i += 2
                continue
            raise ValueError(
                f"({name} ...) unrecognised keyword {elem!r} in capture body")
        constraints.append(parse_matcher(elem))
        i += 1

    if len(constraints) > 1:
        raise ValueError(
            f"({name} ...) named capture accepts at most one constraint;"
            f" got {len(constraints)}")
    sub = constraints[0] if constraints else None
    return CAPTURE(name, sub)


# === Built-in factory helpers ===


def expect_arity(name, items, expected):
    """
    Assert that items has exactly expected length; raise ValueError
    on mismatch
    """

    if len(items) != expected:
        raise ValueError(
            f"({name} ...) expects {expected} argument(s), got {len(items)}")


def coerce_slot(name, value):
    """
    Coerce a surface slot-name element to a Python str. Accepts a
    koskari symbol or a Python str
    """

    if is_symbol(value):
        return str(value)
    if isinstance(value, str):
        return value
    raise ValueError(
        f"({name} ...) slot name must be a symbol or string, got {type(value).__name__!r}")


def coerce_str_arg(name, value):
    """
    Coerce a surface single-string arg (used for TYPE, PREDICATE,
    LITERAL_KW, LITERAL_SYM) to a Python str. Accepts symbol, str,
    or a koskari keyword (whose name is used)
    """

    if is_symbol(value) or is_keyword(value):
        return str(value)
    if isinstance(value, str):
        return value
    raise ValueError(
        f"({name} ...) argument must be a symbol, keyword, or string,"
        f" got {type(value).__name__!r}")


def slot_only_factory(name, ctor):
    """
    Build a factory for a slot-only composite: (NAME slot)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(coerce_slot(name, items[0]))

    factory.__name__ = f"factory_{name}"
    factory.matcher_wire_class = ctor
    return factory


def unary_recursive_factory(name, ctor):
    """
    Build a factory for a unary-recursive head, e.g. (NOT pat),
    (OPTIONAL pat)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(parse_matcher(items[0]))

    factory.__name__ = f"factory_{name}"
    return factory


_MATCH_MIN_KW = frozenset({"min"})


def _min_option_key(elem):
    """
    Normalize a Koskari keyword to ``min`` for ``min:`` / ``min``
    """

    return str(elem).rstrip(":")


def repeat_match_factory(head_sym, raw_args):
    """
    Lower (REPEAT pat) or (REPEAT slot pat min: N ...).
    Two-or-more args use repeat_capture(slot, REPEAT(inner, min_count=...)).
    """

    items = list(unpack(raw_args))
    if len(items) == 1:
        return REPEAT(parse_matcher(items[0]))
    if len(items) < 2:
        raise ValueError(
            "(REPEAT ...) requires a pattern, or a slot and a pattern")
    slot = coerce_slot("REPEAT", items[0])
    inner = parse_matcher(items[1])
    min_count = 1
    i = 2
    n = len(items)
    while i < n:
        elem = items[i]
        if not is_keyword(elem):
            raise ValueError(
                "(REPEAT ...) trailing arguments must be keyword/value pairs,"
                f" got {elem!r}")
        if _min_option_key(elem) not in _MATCH_MIN_KW:
            raise ValueError(
                f"(REPEAT ...) unknown option {elem!r}")
        if i + 1 >= n:
            raise ValueError(
                f"(REPEAT ...) option {elem!r} missing value")
        mv = items[i + 1]
        if isinstance(mv, bool) or not isinstance(mv, int):
            raise TypeError(
                f"(REPEAT ...) min value must be int, got {type(mv).__name__!r}")
        if mv < 0:
            raise ValueError(
                f"(REPEAT ...) min must be non-negative, got {mv}")
        min_count = mv
        i += 2
    return repeat_capture(slot, REPEAT(inner, min_count=min_count))


def rest_match_factory(head_sym, raw_args):
    """
    Lower (REST), (REST slot), or (REST slot min: N). Default min_tail is
    0; bare (REST) has no capture slot
    """

    items = list(unpack(raw_args))
    if not items:
        return REST(None, min_tail=0)
    slot = coerce_slot("REST", items[0])
    min_tail = 0
    i = 1
    n = len(items)
    while i < n:
        elem = items[i]
        if not is_keyword(elem):
            raise ValueError(
                "(REST ...) trailing arguments must be keyword/value pairs,"
                f" got {elem!r}")
        if _min_option_key(elem) not in _MATCH_MIN_KW:
            raise ValueError(
                f"(REST ...) unknown option {elem!r}")
        if i + 1 >= n:
            raise ValueError(
                f"(REST ...) option {elem!r} missing value")
        mv = items[i + 1]
        if isinstance(mv, bool) or not isinstance(mv, int):
            raise TypeError(
                f"(REST ...) min value must be int, got {type(mv).__name__!r}")
        if mv < 0:
            raise ValueError(
                f"(REST ...) min must be non-negative, got {mv}")
        min_tail = mv
        i += 2
    return REST(slot, min_tail=min_tail)


def tail_match_factory(head_sym, raw_args):
    """
    Lower (TAIL slot) to OPTIONAL(REST(slot, min_tail=1))
    """

    items = list(unpack(raw_args))
    expect_arity("TAIL", items, 1)
    slot = coerce_slot("TAIL", items[0])
    return OPTIONAL(REST(slot, min_tail=1))


def _lower_spine_children(raw_args, child_parser=None):
    """
    Lower the child patterns of a spine composite (FORM / MATCH),
    recognizing the ``*: name`` rest marker: a ``*:`` keyword followed by
    a slot name lowers to a named REST capture. Every other child is
    lowered through ``child_parser`` (parse_matcher by default; the
    runtime case-pattern dialect passes parse_case_element).
    """

    if child_parser is None:
        child_parser = parse_matcher
    items = list(unpack(raw_args))
    out = []
    i = 0
    n = len(items)
    while i < n:
        elem = items[i]
        if elem == KW_STAR:
            if i + 1 >= n:
                raise ValueError("`*:` must be followed by a rest slot name")
            out.append(REST(coerce_slot("*:", items[i + 1])))
            i += 2
        else:
            out.append(child_parser(elem))
            i += 1
    return out


def form_factory(head_sym, raw_args):
    """
    Lower (FORM pat) and (FORM pat1 pat2 ...). The single-arg form
    becomes FORM(parse_matcher(pat)); the multi-arg form implicitly
    wraps the children in MATCH per the rollup convention. A ``*: name``
    child lowers to a named REST capture
    """

    items = _lower_spine_children(raw_args)
    if not items:
        raise ValueError("(FORM ...) requires at least one sub-pattern")
    if len(items) == 1:
        return FORM(items[0])
    return FORM(MATCH(*items))


def match_factory(head_sym, raw_args):
    """
    Lower (MATCH pat...). Empty MATCH is permitted (matches zero
    elements). A ``*: name`` child lowers to a named REST capture
    """

    return MATCH(*_lower_spine_children(raw_args))


def group_named_factory(head_sym, raw_args):
    """
    Lower (GROUP slot pat...) or (GROUP nil pat...) for an anonymous
    value-only group row (slot names the nested dict key when named)
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(GROUP ...) requires at least one argument")
    first = items[0]
    if is_nil(first):
        return GROUP(None, *[parse_matcher(i) for i in items[1:]])
    slot = coerce_slot("GROUP", first)
    return GROUP(slot, *[parse_matcher(i) for i in items[1:]])


def oneof_factory(head_sym, raw_args):
    """
    Lower (ONEOF pat...). Requires at least one branch (mirrors the
    ONEOF builder)
    """

    items = [parse_matcher(i) for i in unpack(raw_args)]
    if not items:
        raise ValueError("(ONEOF ...) requires at least one branch")
    return ONEOF(*items)


def list_factory(head_sym, raw_args):
    """
    Lower (LIST pat...). LIST itself wraps in FORM(MATCH(...)) so
    extra wrapping here is unnecessary
    """

    return LIST(*[parse_matcher(i) for i in unpack(raw_args)])


def capture_factory(head_sym, raw_args):
    """
    Lower explicit (CAPTURE slot args...). Delegates to
    lower_named_capture so the constraint splitting matches
    the implicit named-capture form
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(CAPTURE ...) requires a slot name")
    slot_sym = items[0]
    if not is_symbol(slot_sym):
        raise ValueError(
            f"(CAPTURE ...) slot must be a symbol, got {slot_sym!r}")
    _, tail = raw_args
    return lower_named_capture(slot_sym, tail)


def literal_factory(head_sym, raw_args):
    """
    Lower (LITERAL value). The value is taken raw without recursion
    through parse_matcher (so symbols and lists become themselves,
    not capture or match patterns)
    """

    items = list(unpack(raw_args))
    expect_arity("LITERAL", items, 1)
    return LITERAL(items[0])


def test_any_factory(head_sym, raw_args):
    """
    Lower (TEST_ANY)
    """

    items = list(unpack(raw_args))
    expect_arity("TEST_ANY", items, 0)
    return TEST_ANY()


def str_arg_factory(name, ctor):
    """
    Build a factory for a head taking exactly one stringy argument
    (TYPE, PREDICATE, LITERAL_KW, LITERAL_SYM)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(coerce_str_arg(name, items[0]))

    factory.__name__ = f"factory_{name}"
    return factory


def app_factory(head_sym, raw_args):
    """
    Lower (APP fn) or (APP fn pat). fn is the extractor's extern name
    (a symbol); the optional pat matches the call's result
    """

    items = list(unpack(raw_args))
    if not items or len(items) > 2:
        raise ValueError(
            f"(APP ...) expects 1 or 2 arguments, got {len(items)}")
    fn = coerce_slot("APP", items[0])
    if len(items) == 1:
        return APP(fn)
    return APP(fn, parse_matcher(items[1]))


def field_factory(head_sym, raw_args):
    """
    Lower (FIELD pat) or (FIELD NAME pat). Single-arg form becomes
    a positional field; two-arg form binds the keyword field name
    to NAME (which must be a symbol)
    """

    items = list(unpack(raw_args))
    if len(items) == 1:
        return FIELD(parse_matcher(items[0]))
    if len(items) == 2:
        first = items[0]
        if not is_symbol(first):
            raise ValueError(
                f"(FIELD NAME pat) NAME must be a symbol, got {first!r}")
        # the name reaches getattr and carries no marks: a template-written
        # NAME is a gensym, read by its base as the case dialect does
        return FIELD(parse_matcher(items[1]), name=str(strip_mark(first)))
    raise ValueError(
        f"(FIELD ...) expects 1 or 2 arguments, got {len(items)}")


def register_builtin_matchers():
    """
    Seed MATCHER_REGISTRY with factories for every built-in surface
    head. Idempotent at module load
    """

    register_matcher("FORM", form_factory)
    register_matcher("MATCH", match_factory)
    register_matcher("GROUP", group_named_factory)
    register_matcher("ONEOF", oneof_factory)
    register_matcher("LIST", list_factory)
    register_matcher("NOT", unary_recursive_factory("NOT", NOT))
    register_matcher("REPEAT", repeat_match_factory)
    register_matcher(
        "OPTIONAL", unary_recursive_factory("OPTIONAL", OPTIONAL))

    register_matcher("CAPTURE", capture_factory)
    register_matcher("REST", rest_match_factory)
    register_matcher("TAIL", tail_match_factory)
    register_matcher("BIND", slot_only_factory("BIND", BIND))
    register_matcher("VALUE", slot_only_factory("VALUE", VALUE))
    register_matcher("KEYWORD", slot_only_factory("KEYWORD", KEYWORD))
    register_matcher("SYMBOL", slot_only_factory("SYMBOL", SYMBOL))
    register_matcher("PAIR", slot_only_factory("PAIR", PAIR))
    register_matcher("BODY", slot_only_factory("BODY", BODY))

    register_matcher("DEFINE", slot_only_factory("DEFINE", DEFINE))
    register_matcher("DECL", slot_only_factory("DECL", DECL))
    register_matcher("BINDING", slot_only_factory("BINDING", BINDING))
    register_matcher("BINDINGS", slot_only_factory("BINDINGS", BINDINGS))
    register_matcher("FORMALS", slot_only_factory("FORMALS", FORMALS))
    register_matcher("USING", slot_only_factory("USING", USING))

    register_matcher("LITERAL", literal_factory)
    register_matcher(
        "LITERAL_KW", str_arg_factory("LITERAL_KW", LITERAL_KW))
    register_matcher(
        "LITERAL_SYM", str_arg_factory("LITERAL_SYM", LITERAL_SYM))
    register_matcher("TYPE", str_arg_factory("TYPE", TYPE))
    register_matcher("PREDICATE", str_arg_factory("PREDICATE", PREDICATE))
    register_matcher("TEST_ANY", test_any_factory)
    register_matcher("FIELD", field_factory)
    register_matcher("APP", app_factory)


register_builtin_matchers()


# === Runtime case-pattern dialect (match/case surface) ===
#
# The runtime match/case surface classifies case patterns the same way
# parse_matcher classifies matcher syntax: an ordered ONEOF meta-pattern
# recognizes one pattern element, and a walker dispatches on which
# capture slot bound, recursing one level at a time. Pattern *kind* is
# decided entirely by head symbol or marker -- reader sugars mint the
# distinguishing heads (#list / #tuple for sequences, item for class,
# #dict for mappings) -- never by cell or bracket type. See
# design/archive/match-pattern-language.md.


CASE_ELEMENT_PATTERN = ONEOF(
    # 'sym -- literal symbol
    FORM(MATCH(
        LITERAL_SYM("quote"),
        CAPTURE("quoted_symbol", TYPE("symbol")))),

    # sequence sugar: (#list ...) / (#tuple ...); the head capture keeps
    # the branch distinguishable even when the element list is empty
    FORM(MATCH(
        CAPTURE("seq_head",
                ONEOF(LITERAL_SYM("#list"), LITERAL_SYM("#tuple"))),
        REST("seq_elems"))),

    # mapping sugar: (#dict (#tuple KEY VAL) ...)
    FORM(MATCH(
        CAPTURE("map_head", LITERAL_SYM("#dict")),
        REST("map_entries"))),

    # class sugar: (item Name field...)
    FORM(MATCH(
        CAPTURE("class_head", LITERAL_SYM("item")),
        CAPTURE("class_name", TYPE("symbol")),
        REST("class_fields"))),

    # combinators
    FORM(MATCH(
        CAPTURE("or_head", LITERAL_SYM("or")),
        REST("or_branches"))),
    FORM(MATCH(LITERAL_SYM("not"), CAPTURE("not_operand"))),
    FORM(MATCH(LITERAL_SYM("?"), CAPTURE("pred_name", TYPE("symbol")))),

    # computed extraction: (app f) / (app f pat)
    FORM(MATCH(
        LITERAL_SYM("app"),
        CAPTURE("app_fn", TYPE("symbol")),
        CAPTURE("app_pat"))),
    FORM(MATCH(
        LITERAL_SYM("app"),
        CAPTURE("app_fn", TYPE("symbol")))),

    # marked typed element: (name :: ...) / (:: ...); the whole form is
    # re-fed to parse_matcher, which owns the constraint grammar
    FORM(MATCH(
        CAPTURE("marked_name", TYPE("symbol")),
        LITERAL_KW("::"),
        REST("marked_args"))),
    FORM(MATCH(
        CAPTURE("anon_marker", LITERAL_KW("::")),
        REST("marked_args"))),

    # explicit matcher heads (CLASS, FORM, ONEOF, REST, ...); the walker
    # checks the registry -- a bare (a b c) is never a silent sequence
    FORM(MATCH(
        CAPTURE("known_head", TYPE("symbol")),
        REST("known_args"))),

    # atoms
    CAPTURE("atom_symbol", TYPE("symbol")),
    CAPTURE("atom_literal"),
)


CASE_ELEMENT_MATCHER = demand(make_matcher(MATCH(CASE_ELEMENT_PATTERN)))


CASE_DELIM_PATTERN = ONEOF(
    # one form inside the delimiter: the pattern itself
    FORM(MATCH(CAPTURE("single"))),

    # multi-token marked pattern: [[x :: int]] / [[:: int]]
    FORM(MATCH(
        CAPTURE("multi_name", TYPE("symbol")),
        LITERAL_KW("::"),
        REST("multi_args"))),
    FORM(MATCH(
        CAPTURE("multi_marker", LITERAL_KW("::")),
        REST("multi_args"))),
)


CASE_DELIM_MATCHER = demand(make_matcher(MATCH(CASE_DELIM_PATTERN)))


_symbol_hash_tuple = symbol("#tuple")


# expected shapes for the case-dialect heads, used to diagnose an
# arity / argument-shape mistake that fell past the meta-branches
_CASE_DIALECT_USAGE = {
    "app": "(app extractor) or (app extractor PATTERN)",
    "?": "(? predicate) with a symbol predicate name",
    "not": "(not PATTERN)",
    "or": "(or PATTERN ...)",
    "item": "(item ClassName field...) -- the ClassName[field...] sugar",
}


def _lower_case_map(entries):
    """
    Lower the entries of a mapping sugar pattern ((#dict (#tuple KEY
    VAL) ...), written #{KEY: VAL ...}). A symbol key is an extern
    resolved from the namespace at match time (the same channel as type
    and class names); any other atom is a literal key. Each value is a
    case-pattern element for the looked-up value
    """

    lowered = []
    for entry in unpack(entries):
        items = list(unpack(entry)) if is_pair(entry) else []
        if len(items) != 3 or items[0] is not _symbol_hash_tuple:
            raise ValueError(
                f"#{{...}} pattern entry must be a key: value pair,"
                f" got {entry!r}")
        key, val = items[1], items[2]
        sub = parse_case_element(val)
        if is_symbol(key):
            lowered.append(MAP_ENTRY(str(key), sub, extern=True))
        elif is_pair(key):
            raise ValueError(
                f"#{{...}} pattern key must be an atom, got {key!r}")
        else:
            lowered.append(MAP_ENTRY(key, sub))
    return MAP(*lowered)


def _lower_case_class(name_sym, fields):
    """
    Lower the fields of a class sugar pattern ((item Name field...),
    written Name[field...]) into CLASS(name, FIELD...). Python-style
    field rules: elements before the first keyword are positional
    fields (matched via __match_args__); ``k: pat`` is a keyword field
    on attribute k; a bare symbol after keywords have started is the
    self-binding keyword field ``k: k``. Positional fields may not
    follow keyword fields
    """

    # the class name is a reference the use site resolves and a field
    # keyword names an attribute: both read by their bare spelling. A
    # self-binding field binds under its marked spelling and names the
    # attribute under the bare one
    type_name = str(name_sym.base)
    field_objs = []
    items = list(unpack(fields))
    seen_keyword = False
    i = 0
    n = len(items)
    while i < n:
        elem = items[i]
        tag = strip_mark(elem)
        if is_keyword(tag):
            if i + 1 >= n:
                raise ValueError(
                    f"{type_name}[...] keyword field"
                    f" {tag!s} missing its pattern")
            field_objs.append(FIELD(
                parse_case_element(items[i + 1]),
                name=str(tag).rstrip(":")))
            seen_keyword = True
            i += 2
        elif seen_keyword:
            if not is_symbol(tag):
                raise ValueError(
                    f"{type_name}[...] field after keyword fields"
                    f" must be a symbol, got {elem!r}")
            field_objs.append(FIELD(CAPTURE(str(elem)), name=str(tag)))
            i += 1
        else:
            field_objs.append(FIELD(parse_case_element(elem)))
            i += 1
    return CLASS(type_name, *field_objs)


def parse_case_element(elem):
    """
    Lower one runtime case-pattern element to a matcher pattern object.
    Runs CASE_ELEMENT_MATCHER against (elem,), then dispatches on which
    capture slot bound. Raises ValueError when elem does not satisfy any
    case-pattern surface form

    :param elem: one koskari source element (atom or pair-tree)
    :rtype: Pattern
    """

    if is_nil(elem):
        raise ValueError(
            "nil is not a valid case pattern element")
    c = CASE_ELEMENT_MATCHER(cons(elem, nil))
    if c is None:
        raise ValueError(
            f"not a valid case pattern: {elem!r}")

    if "atom_symbol" in c:
        return CAPTURE(str(c["atom_symbol"].datum))

    if "atom_literal" in c:
        atom = c["atom_literal"].datum
        if isinstance(atom, pair):
            raise ValueError(
                f"improper list is not a valid case pattern: {atom!r}")
        return LITERAL(atom)

    if "quoted_symbol" in c:
        return LITERAL_SYM(str(c["quoted_symbol"].datum.base))

    if "seq_head" in c:
        elems = c["seq_elems"].datum if "seq_elems" in c else nil
        return FORM(MATCH(
            *_lower_spine_children(elems, parse_case_element)))

    if "map_head" in c:
        entries = c["map_entries"].datum if "map_entries" in c else nil
        return _lower_case_map(entries)

    if "class_head" in c:
        name = c["class_name"].datum
        fields = c["class_fields"].datum if "class_fields" in c else nil
        return _lower_case_class(name, fields)

    if "or_head" in c:
        if "or_branches" not in c:
            raise ValueError("(or ...) requires at least one branch")
        branches = [
            parse_case_element(e)
            for e in unpack(c["or_branches"].datum)]
        return ONEOF(*branches)

    if "not_operand" in c:
        # the engine's NOT is a zero-width lookahead; the case dialect's
        # (not p) is a whole-element pattern, so pair the check with a
        # consuming any -- the element must be present and must not
        # match p. (The canonical registry (NOT ...) head keeps the raw
        # zero-width semantics.)
        return MATCH(
            NOT(parse_case_element(c["not_operand"].datum)),
            TEST_ANY())

    if "pred_name" in c:
        return PREDICATE(str(c["pred_name"].datum))

    if "app_fn" in c:
        fn = str(c["app_fn"].datum)
        if "app_pat" in c:
            return APP(fn, parse_case_element(c["app_pat"].datum))
        return APP(fn)

    if "marked_name" in c or "anon_marker" in c:
        return parse_matcher(elem)

    if "known_head" in c:
        head = c["known_head"].datum
        name = str(head)
        if name in MATCHER_REGISTRY:
            return parse_matcher(elem)
        # a dialect head that missed its meta-branch got the arity or
        # argument shape wrong -- diagnose that, not "unknown head"
        usage = _CASE_DIALECT_USAGE.get(name)
        if usage is not None:
            raise ValueError(
                f"malformed ({name} ...) case pattern {elem!r};"
                f" expected {usage}")
        raise ValueError(
            f"unknown case pattern head {name};"
            " sequence patterns use #[...] or #(...)")

    raise ValueError(
        f"internal: case meta-matcher matched with no slot bound: {elem!r}")


def parse_case_pattern(content):
    """
    Lower the content of one match/case clause delimiter to a matcher
    pattern object. The delimiter ([[PATTERN] BODY...]) holds either a
    single pattern form, or the multi-token spelling of a marked typed
    pattern (x :: int); a bare multi-token content is an error -- it is
    never a silent sequence

    :param content: the delimiter list (pair-tree)
    :rtype: Pattern
    """

    if is_nil(content):
        raise ValueError("empty case pattern")
    c = CASE_DELIM_MATCHER(cons(content, nil))
    if c is None:
        raise ValueError(
            f"invalid case pattern {content!r};"
            " sequence patterns use #[...] or #(...)")

    if "single" in c:
        return parse_case_element(c["single"].datum)

    if "multi_name" in c or "multi_marker" in c:
        return parse_matcher(content)

    raise ValueError(
        f"internal: case delimiter matched with no slot bound: {content!r}")

# The end.
