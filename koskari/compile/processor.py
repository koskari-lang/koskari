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
koskari.compile.processor

The bytecode processor internalizes an instruction stream and applies
the gated transformation pipelines: peephole squish, feeder fusion, VAR
run packing, operand normalization, VAR to FAST lowering, and generator
tail-call demotion. Produces the finished rows for Code construction

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import operator

from ..lib import (Compare, NAMESPACE_FAST_SLOT_MAX, NamespaceSpec,
                   OpCode, Operand, OpStyle)


OPERAND_COUNT = len(Operand)
COMPARE_COUNT = len(Compare)


def merge_packed(value, label, selector_count):
    """
    Normalize a packed-uint16 operand -- a (selector, count) pair or a
    pre-merged int -- validating the selector against selector_count
    and the count as a byte

    :param value: (selector, count) pair, or a pre-merged int
    :param label: operand kind name for error messages
    :param selector_count: exclusive upper bound for the selector
    :return: merged uint16 int
    :raises ValueError: on a bad shape, out-of-range byte, or unknown
        selector
    """

    if isinstance(value, tuple):
        if len(value) != 2:
            raise ValueError(f"{label} operand must be a "
                             f"(selector, count) pair, got {value!r}")
        sel, count = value
        sel = int(getattr(sel, 'value', sel))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError(f"{label} int out of uint16 range: {value!r}")
        sel, count = merged >> 8, merged & 0xff

    if not 0 <= sel < selector_count:
        raise ValueError(f"{label} selector out of range: {sel!r}")
    if not 0 <= count <= 0xff:
        raise ValueError(f"{label} count out of byte range: {count!r}")

    return (sel << 8) | count


def merge_opcount(value):
    """
    Normalize an OPCOUNT-style operand to its merged uint16 (operator
    selector in the high byte, reduce count in the low byte). Accepts
    either idiom, so specials may emit either a (op_type, count) pair
    or a pre-merged int, both range- and op-type-checked
    """

    return merge_packed(value, 'opcount', OPERAND_COUNT)


def merge_cmpcount(value):
    """
    Normalize a CMPCOUNT-style operand to its merged uint16 (Compare
    selector in the high byte, reduce count in the low byte); the
    CMPCOUNT twin of merge_opcount
    """

    return merge_packed(value, 'cmpcount', COMPARE_COUNT)


def split_cmpcount(value):
    """
    Split a CMPCOUNT operand -- (selector, count) pair or merged int --
    into a validated (selector, count) int pair.
    """

    merged = merge_cmpcount(value)
    return (merged >> 8, merged & 0xff)


# CMPSRC packing (mirrors the KosCmpSrc_* macros in bytecode.h):
# selector in bits 0-3, per-operand constant-source bits above.
CMPSRC_X_CONST = 0x10
CMPSRC_Y_CONST = 0x20


def make_cmpsrc(sel, x_const, y_const):
    """
    Pack a CMPSRC field from a Compare selector and two source flags
    """

    return ((int(getattr(sel, 'value', sel)) & 0x0f)
            | (CMPSRC_X_CONST if x_const else 0)
            | (CMPSRC_Y_CONST if y_const else 0))


CHECK_SQUASHES = {
    OpCode.OBJECT_NOT: OpCode.JUMP_TRUE,
    OpCode.EXCEPTION_MATCH: OpCode.CHECK_EXCEPTION,
    OpCode.NILP: OpCode.CHECK_NIL,
}


SWITCH_SQUASHES = {
    OpCode.CONST: OpCode.SWITCH_EQ_CONST,
    OpCode.VAR: OpCode.SWITCH_EQ_VAR,
    OpCode.VAR_ATTR: OpCode.SWITCH_EQ_VAR_ATTR,
}


# pattern atoms for rule declarations: sets match any member, a bare
# opcode matches itself
JUMP_PRIORS = frozenset((OpCode.JUMP_TRUE, OpCode.JUMP_FALSE))
POP_PRIORS = frozenset((OpCode.CONST, OpCode.DUP))
FEEDER = frozenset((OpCode.VAR, OpCode.CONST))
CONST_FEEDER = frozenset((OpCode.CONST,))


def chain_fold(pyop):
    """
    Build a chain-compare fold over pyop: True when every adjacent
    operand pair satisfies the comparison
    """

    def fold(values):
        for x, y in zip(values, values[1:]):
            if not pyop(x, y):
                return False
        return True
    return fold


def membership_fold(negate):
    """
    Build an arity-2 membership fold; negate selects the not-in sense

    :raises TypeError: on any other arity, declining the fold so the
        runtime raises its own arity error
    """

    def fold(values):
        if len(values) != 2:
            raise TypeError("membership comparison requires 2 operands")
        found = values[0] in values[1]
        return not found if negate else found
    return fold


# Compare selector -> compile-time fold for compare_const_fold. The
# identity and type-check selectors are deliberately absent: IS/IS_NOT
# depend on object identity, which folding cannot promise across code
# serialization, and IS_INSTANCE/IS_SUBCLASS never see constant class
# operands from the compiler.
COMPARE_FOLDS = {
    Compare.LT: chain_fold(operator.lt),
    Compare.LE: chain_fold(operator.le),
    Compare.EQ: chain_fold(operator.eq),
    Compare.NE: chain_fold(operator.ne),
    Compare.GT: chain_fold(operator.gt),
    Compare.GE: chain_fold(operator.ge),
    Compare.IN: membership_fold(False),
    Compare.NOT_IN: membership_fold(True),
}


def build_var_to_fast():
    # Pre-compiled map: VAR opcode -> FAST opcode for convert_fast lowering.
    # Built by finding each FAST_* opcode and resolving to a matching VAR_*
    # opcode.

    var_to_fast = {}
    for opcode in OpCode:
        opname = opcode.name
        if not (opname.startswith('FAST')
                or opname.startswith('CALL_FAST')
                or opname.startswith('TAIL_CALL_FAST')):
            continue
        varname = opname.replace('FAST', 'VAR', 1)
        if hasattr(OpCode, varname):
            var_to_fast[OpCode[varname]] = opcode

    var_to_fast[OpCode.UNPACK_VAR] = OpCode.UNPACK_FAST
    var_to_fast[OpCode.SWITCH_EQ_VAR] = OpCode.SWITCH_EQ_FAST
    var_to_fast[OpCode.SWITCH_EQ_VAR_ATTR] = OpCode.SWITCH_EQ_FAST_ATTR
    var_to_fast[OpCode.OBJECT_COMPARE_VAR] = OpCode.OBJECT_COMPARE_FAST

    return var_to_fast


VAR_TO_FAST = build_var_to_fast()


# Name-derived relabel pairs: every TAIL_CALL* opcode twins the CALL*
# opcode named by stripping the TAIL_ prefix, carrying the same operand
# schema and stack contract.
TAIL_CALL_TO_CALL = {
    opcode: OpCode[opcode.name[len('TAIL_'):]]
    for opcode in OpCode
    if opcode.name.startswith('TAIL_CALL')
}


CALL_TO_TAIL_CALL = {
    call: tail
    for tail, call in TAIL_CALL_TO_CALL.items()
}


# Extent classification for tail-call promotion, by the uninstall
# principle: an extent whose uninstall behavior must run after the
# call cannot be tail-called through. Discardable extents (True) are
# safely dropped by frame handoff.
EXTENT_DISCARDABLE = {
    OpCode.PUSH_NS: True,
    OpCode.PUSH_LOOP: True,
    OpCode.PUSH_FOREACH: True,
    OpCode.PUSH_COLLECT: False,
    OpCode.PUSH_TRY: False,
    OpCode.PUSH_FINALLY: False,
    OpCode.PUSH_WITH: False,
    OpCode.PUSH_DYN_WITH: False,
    OpCode.PUSH_WIND: False,
    OpCode.PUSH_PROMPT: False,
    OpCode.PUSH_SHIFT: False,
    OpCode.PUSH_CC: False,
    OpCode.PUSH_PARAM: False,
}


EXTENT_POPS = frozenset((
    OpCode.POP_NS, OpCode.POP_LOOP, OpCode.POP_FOREACH,
    OpCode.POP_COLLECT, OpCode.POP_TRY, OpCode.POP_FINALLY,
    OpCode.POP_WITH, OpCode.POP_DYN_WITH, OpCode.POP_WIND,
    OpCode.POP_PROMPT, OpCode.POP_SHIFT, OpCode.POP_CC,
    OpCode.POP_PARAM,
))


# rows the tail-call route reads through on its way to RETURN 1
ROUTE_TRANSPARENT = frozenset((
    OpCode.LABEL, OpCode.POSITION, OpCode.NOOP)) | EXTENT_POPS


# The per-pass rule registries: trigger opcode -> candidate list of
# (priors, rule method). Sight reading a pass means reading its
# registered rules in source order.
SQUISH_RULES = {}
FUSE_RULES = {}


def squish_rule(*pattern):
    """
    Register a processor method as a squish rule for the form given in
    stream order: the lookback slot specs (an opcode, or a set of
    opcodes) followed by the trigger opcode. Candidates for a trigger
    are tried in registration order; the first whose form matches and
    whose rule accepts is applied. A trigger-only pattern registers a
    fallback rule that inspects the working rows itself.
    """

    *priors, trigger = pattern
    priors = tuple(
        spec if isinstance(spec, frozenset) else frozenset((spec,))
        for spec in priors)

    def register(fn):
        SQUISH_RULES.setdefault(trigger, []).append((priors, fn))
        return fn
    return register


def fuse_rule(*pattern):
    """
    Register a processor method as a fusion rule for the form given in
    stream order: the lookback slot specs (an opcode, or a set of
    opcodes) followed by the trigger opcode. Candidates for a trigger
    are tried in registration order; the first whose form matches and
    whose rule accepts is applied. A trigger-only pattern registers a
    fallback rule that inspects the working rows itself.
    """

    *priors, trigger = pattern
    priors = tuple(
        spec if isinstance(spec, frozenset) else frozenset((spec,))
        for spec in priors)

    def register(fn):
        FUSE_RULES.setdefault(trigger, []).append((priors, fn))
        return fn
    return register


class ByteCodeProcessor:
    """
    A single-use transformation machine over one instruction stream.

    Constructed per code object with the rows, the pragma-derived gate
    flags, and the compilation mode; mutated in place as passes apply;
    discarded after the finished rows are taken. Rules and pass tables
    are stateless module-level data. The instance carries only the
    stream and its per-stream working state, so its lifetime is exactly
    the validity of that state.
    """

    def __init__(self, instructions, gates=None, mode=None):
        """
        :param instructions: sequence of (op, arg) rows, internalized
        :param gates: optional mapping of pass-gate flags; recognized
            key 'convert-fast' enables the VAR to FAST lowering
        :param mode: CompilerMode for the stream, consulted by the
            fast lowering
        """

        self.instructions = list(instructions)
        self.gates = dict(gates) if gates else {}
        self.mode = mode

        # the output accumulator of the pass currently walking the
        # stream; the matcher and splice services address it
        self.rows = None


    def splice(self, start, skipped, replacement):
        """
        Replace the working rows from start onward with replacement,
        re-emitting the latest stepped-over row (by index) afterward so
        following instructions keep their position
        """

        rows = self.rows
        trailing = rows[max(skipped)] if skipped else None
        del rows[start:]
        rows.extend(replacement)
        if trailing is not None:
            rows.append(trailing)


    def match_priors(self, priors):
        """
        Match a rule's declared prior slots against the top of the
        working rows, bottom-most slot first; POSITION rows are stepped
        over. Returns (found, skipped) with found in pattern order, or
        (None, skipped) when the shape does not match.
        """

        posop = OpCode.POSITION
        rows = self.rows
        found = []
        skipped = []
        i = len(rows) - 1
        for spec in reversed(priors):
            while i >= 0 and rows[i][0] == posop:
                skipped.append(i)
                i -= 1
            if i < 0 or rows[i][0] not in spec:
                return None, skipped
            found.append(i)
            i -= 1
        found.reverse()
        return found, skipped


    def apply_rules(self, rules, rows):
        """
        Walk the incoming instructions dispatching each row against a
        pass's registry: the candidates for the row's opcode are tried
        in registration order, and the first whose declared form
        matches and whose rule accepts is applied.

        A rule receives the matched prior rows in pattern order and
        returns None to pass the form along, a list of rows to splice
        over the matched form, or a rewritten (op, arg) trigger -- the
        matched rows are absorbed and dispatch retries, descending the
        lowering ladder. With no acceptance the row is appended
        unchanged. Retry termination is the rule author's obligation:
        every rewrite must consume rows or produce an op that resolves
        without further rewriting.
        """

        self.rows = rows
        for op, arg in self.instructions:
            while True:
                outcome = found = skipped = None
                for priors, fn in rules.get(op, ()):
                    found, skipped = self.match_priors(priors)
                    if found is None:
                        continue
                    outcome = fn(self, op, arg,
                                 [rows[i] for i in found])
                    if outcome is not None:
                        break
                if outcome is None:
                    rows.append((op, arg))
                    break
                start = found[0] if found else len(rows)
                if isinstance(outcome, list):
                    self.splice(start, skipped, outcome)
                    break
                self.splice(start, skipped, [])
                op, arg = outcome
        self.rows = None
        return rows


    @squish_rule(POP_PRIORS, OpCode.POP)
    def pop_pushed_value(self, op, arg, matched):
        """
        CONST|DUP POP1 -> nothing: both rows vanish
        """

        if arg != 1:
            return None
        return []


    @squish_rule(OpCode.CONST, OpCode.OBJECT_NOT)
    def not_const_fold(self, op, arg, matched):
        """
        CONST OBJECT_NOT -> the negated constant
        """

        return [(OpCode.CONST, not matched[0][1])]


    @squish_rule(OpCode.OBJECT_COMPARE)
    def compare_const_fold(self, op, arg, matched):
        """
        CONST xN OBJECT_COMPARE with a foldable selector -> the chain
        evaluated at compile time as a single CONST bool. A fallback
        rule: the feeder count is read from the CMPCOUNT operand, so
        matching and splicing run through the services directly.
        Declines on a non-foldable selector, any non-CONST feeder, or
        a comparison error, leaving the runtime behavior in place.
        """

        sel, cnt = split_cmpcount(arg)
        fold = COMPARE_FOLDS.get(sel)
        if fold is None or cnt < 2:
            return None

        found, skipped = self.match_priors((CONST_FEEDER,) * cnt)
        if found is None:
            return None

        rows = self.rows
        try:
            result = fold([rows[i][1] for i in found])
        except (TypeError, ValueError, ArithmeticError):
            return None

        self.splice(found[0], skipped, [(OpCode.CONST, result)])
        return []


    @squish_rule(OpCode.CONST, OpCode.JUMP_TRUE)
    def jump_true_const_fold(self, op, arg, matched):
        """
        CONST JUMP_TRUE -> a plain JUMP or nothing
        """

        return [(OpCode.JUMP, arg)] if matched[0][1] else []


    @squish_rule(OpCode.CONST, OpCode.JUMP_FALSE)
    def jump_false_const_fold(self, op, arg, matched):
        """
        CONST JUMP_FALSE -> a plain JUMP or nothing
        """

        return [] if matched[0][1] else [(OpCode.JUMP, arg)]


    @squish_rule(frozenset(CHECK_SQUASHES), OpCode.JUMP_FALSE)
    def jump_false_check_squash(self, op, arg, matched):
        """
        A check-squashable predicate and its JUMP_FALSE combine into
        the single check jump
        """

        return [(CHECK_SQUASHES[matched[0][0]], arg)]


    @squish_rule(OpCode.OBJECT_COMPARE, OpCode.JUMP_FALSE)
    def jump_false_compare_fuse(self, op, arg, matched):
        """
        OBJECT_COMPARE JUMP_FALSE -> CHECK_COMPARE branching on chain
        failure, carrying selector, count, and target in one row
        """

        sel, cnt = split_cmpcount(matched[0][1])
        return [(OpCode.CHECK_COMPARE, (sel, cnt, arg, None))]


    @squish_rule(frozenset(SWITCH_SQUASHES), OpCode.SWITCH_EQ)
    def switch_eq_squash(self, op, arg, matched):
        """
        SWITCH_EQ absorbs its CONST, VAR, or VAR_ATTR comparand into
        the corresponding single-op switch form
        """

        prior, pargs = matched[0]
        if isinstance(pargs, tuple):
            new_arg = (arg, *pargs[:3])
        else:
            new_arg = (arg, pargs, None, None)
        return [(SWITCH_SQUASHES[prior], new_arg)]


    @squish_rule(OpCode.JUMP, OpCode.LABEL)
    def label_jump_elision(self, op, arg, matched):
        """
        JUMP L LABEL L -> the label alone: a jump to the next
        instruction is useless
        """

        if matched[0][1] != arg:
            return None
        return [(op, arg)]


    @squish_rule(OpCode.CONST, JUMP_PRIORS, OpCode.LABEL)
    def label_const_jump_elision(self, op, arg, matched):
        """
        CONST JUMP_TRUE|FALSE L LABEL L -> the label alone: the
        conditional jump to the next instruction vanishes with its
        constant test
        """

        if matched[1][1] != arg:
            return None
        return [(op, arg)]


    @squish_rule(JUMP_PRIORS, OpCode.LABEL)
    def label_dynamic_jump_elision(self, op, arg, matched):
        """
        JUMP_TRUE|FALSE L LABEL L -> POP 1 and the label: the useless
        conditional jump degrades to popping its dynamic test
        """

        if matched[0][1] != arg:
            return None
        return [(OpCode.POP, 1), (op, arg)]


    @squish_rule(OpCode.CONST, OpCode.RETURN)
    @squish_rule(OpCode.CONST, OpCode.BREAK)
    @squish_rule(OpCode.CONST, OpCode.CONTINUE)
    @squish_rule(OpCode.CONST, OpCode.YIELD)
    def control_none_normalize(self, op, arg, matched):
        """
        A control op carrying a just-pushed CONST None normalizes to
        its bare zero-arg form: CONST None, RETURN 1 -> RETURN 0
        """

        if arg != 1 or matched[0][1] is not None:
            return None
        return [(op, 0)]


    @squish_rule(OpCode.CAR, OpCode.CAR)
    @squish_rule(OpCode.CDR, OpCode.CDR)
    def car_cdr_merge(self, op, arg, matched):
        """
        Consecutive CAR or consecutive CDR rows merge their counts
        """

        return [(op, matched[0][1] + arg)]


    @squish_rule(OpCode.CONST, OpCode.VAR_STORE)
    def var_store_const_fuse(self, op, arg, matched):
        """
        CONST VAR_STORE -> the stack-free VAR_STORE_CONST row
        """

        return [(OpCode.VAR_STORE_CONST, (arg, matched[0][1], None, None))]


    @squish_rule(OpCode.CONST, OpCode.VAR_INPLACE)
    def var_inplace_const_fuse(self, op, arg, matched):
        """
        CONST VAR_INPLACE (single operand) -> the stack-free
        VAR_INPLACE_CONST: bare operator selector, name, pooled operand
        """

        merged = merge_opcount(arg[0])
        if merged & 0xff != 1:
            return None
        return [(OpCode.VAR_INPLACE_CONST,
                 (merged >> 8, arg[1], matched[0][1], None))]


    @fuse_rule(FEEDER, FEEDER, OpCode.CHECK_COMPARE)
    def fuse_check_compare(self, op, arg, matched):
        """
        x y CHECK_COMPARE (arity 2, not both feeders constant -- that
        is a folding case) -> CHECK_COMPARE_VAR with a CMPSRC field
        carrying the selector and per-operand source bits
        """

        if arg[1] != 2:
            return None
        x_op, x_arg = matched[0]
        y_op, y_arg = matched[1]
        x_const = x_op == OpCode.CONST
        y_const = y_op == OpCode.CONST
        if x_const and y_const:
            return None
        sel, _cnt, label, _pad = arg
        return [(OpCode.CHECK_COMPARE_VAR,
                 (make_cmpsrc(sel, x_const, y_const), x_arg, y_arg, label))]


    @fuse_rule(OpCode.CONST, OpCode.VAR, OpCode.CONST,
               OpCode.CHECK_COMPARE)
    def fuse_check_bounds(self, op, arg, matched):
        """
        lo x hi CHECK_COMPARE (arity 3 bounds shape) -> the
        bool-producing OBJECT_COMPARE_VAR plus JUMP_FALSE, since the
        fused row cannot also carry the label
        """

        if arg[1] != 3:
            return None
        sel, _cnt, label, _pad = arg
        return [
            (OpCode.OBJECT_COMPARE_VAR,
             (sel, matched[0][1], matched[1][1], matched[2][1])),
            (OpCode.JUMP_FALSE, label),
        ]


    @fuse_rule(OpCode.CONST, OpCode.VAR, OpCode.CONST,
               OpCode.OBJECT_COMPARE)
    def fuse_compare_bounds(self, op, arg, matched):
        """
        lo x hi OBJECT_COMPARE (arity 3 bounds shape) in value position
        -> the bool-producing OBJECT_COMPARE_VAR
        """

        sel, cnt = split_cmpcount(arg)
        if cnt != 3:
            return None
        return [(OpCode.OBJECT_COMPARE_VAR,
                 (sel, matched[0][1], matched[1][1], matched[2][1]))]


    @fuse_rule(OpCode.VAR, OpCode.OBJECT_ITER)
    def fuse_iter(self, op, arg, matched):
        """
        VAR OBJECT_ITER -> VAR_ITER
        """

        return [(OpCode.VAR_ITER, matched[0][1])]


    @fuse_rule(OpCode.VAR, OpCode.OBJECT_NEXT)
    def fuse_next(self, op, arg, matched):
        """
        VAR OBJECT_NEXT (1-arg) -> VAR_NEXT
        """

        if arg != 1:
            return None
        return [(OpCode.VAR_NEXT, (matched[0][1], 1, None, None))]


    @fuse_rule(OpCode.VAR, FEEDER, OpCode.OBJECT_NEXT)
    def fuse_next_default(self, op, arg, matched):
        """
        VAR default OBJECT_NEXT (2-arg) -> the default row plus
        VAR_NEXT, which pops it. Absorbing the iterator load moves its
        name resolution after the default's row, so the fusion requires
        the default to be a bare feeder too.
        """

        if arg != 2:
            return None
        return [
            matched[1],
            (OpCode.VAR_NEXT, (matched[0][1], 2, None, None)),
        ]


    @fuse_rule(OpCode.VAR, OpCode.OBJECT_FORMAT)
    def fuse_format(self, op, arg, matched):
        """
        VAR OBJECT_FORMAT (1-arg) -> VAR_FORMAT
        """

        if arg != 1:
            return None
        return [(OpCode.VAR_FORMAT, matched[0][1])]


    @fuse_rule(OpCode.CONST, OpCode.OBJECT_FORMAT)
    def demote_format_const(self, op, arg, matched):
        """
        CONST OBJECT_FORMAT (2-arg, constant rules) -> demote to
        OBJECT_FORMAT_CONST and retry into the format lowering ladder.
        With dynamic rules the form stays unfused, since absorbing the
        object load would move its name resolution past the rules
        evaluation.
        """

        if arg != 2:
            return None
        return (OpCode.OBJECT_FORMAT_CONST, matched[0][1])


    @fuse_rule(OpCode.VAR, OpCode.OBJECT_FORMAT_CONST)
    def fuse_format_const(self, op, arg, matched):
        """
        VAR OBJECT_FORMAT_CONST -> VAR_FORMAT_CONST
        """

        return [(OpCode.VAR_FORMAT_CONST,
                 (matched[0][1], arg, None, None))]


    def squish(self):
        """
        The peephole half of the compress pipeline: the squish rule set
        over the shared driver, plus trailing RETURN normalization.
        Feeder fusion and VAR run packing follow as separate passes.
        """

        rows = self.apply_rules(SQUISH_RULES, [])

        # trailing RETURN normalization, POSITION-transparent like any
        # other lookback
        i = len(rows) - 1
        while i >= 0 and rows[i][0] == OpCode.POSITION:
            i -= 1
        if i < 0 or rows[i][0] != OpCode.RETURN:
            if i >= 0 and rows[i][0] == OpCode.CONST \
               and rows[i][1] is None:
                rows[i] = (OpCode.RETURN, 0)
            else:
                rows.append((OpCode.RETURN, 1))

        self.instructions = rows


    def fuse_feeders(self):
        """
        The feeder-absorption half of the compress pipeline: the fuse
        rule set over the shared driver. Every pattern requires the
        immediately preceding value rows to be bare VAR or CONST loads;
        POSITION rows are transparent, with the fused row keeping the
        position in effect at the first feeder and the latest interior
        position re-emitted after the row. The fast pass later lowers
        the VAR forms to their FAST twins. Runs before pack_var_runs so
        fusion gets first claim on the feeders.
        """

        self.instructions = self.apply_rules(FUSE_RULES, [])


    def compress(self):
        """
        The compress pipeline: peephole squish, then feeder fusion,
        then VAR run packing, in that order so fusion gets first claim
        on feeder rows
        """

        self.squish()
        self.fuse_feeders()
        self.pack_var_runs()


    def pack_var_runs(self):
        """
        Late sweep: combine runs of consecutive bare VAR loads into VAR4 /
        VAR2 rows (greedy, in that order). Runs after every other squash so
        consumer-specific fusions get first claim on their feeder rows, and
        breaks at any non-VAR instruction (including LABEL). POSITION rows
        inside a run are absorbed. Merged row keeps the position in
        effect at the run's first load, and the latest absorbed position is
        re-emitted after the run so following instructions keep theirs. The
        fast pass later lowers VAR2/VAR4 to FAST2/FAST4 through the
        ordinary VAR->FAST path.
        """

        result = []
        run = []
        trailing_pos = None

        def flush():
            nonlocal trailing_pos
            while len(run) >= 4:
                a, b, c, d = run[:4]
                del run[:4]
                result.append((OpCode.VAR4, (a, b, c, d)))
            if len(run) >= 2:
                a, b = run[:2]
                del run[:2]
                result.append((OpCode.VAR2, (a, b, None, None)))
            if run:
                result.append((OpCode.VAR, run.pop()))
            if trailing_pos is not None:
                result.append(trailing_pos)
                trailing_pos = None

        for op, arg in self.instructions:
            if op == OpCode.VAR:
                run.append(arg)
                continue
            if run and op == OpCode.POSITION:
                trailing_pos = (op, arg)
                continue
            flush()
            result.append((op, arg))

        flush()
        self.instructions = result


    def normalize_opcount(self):
        """
        Merge every OPCOUNT and CMPCOUNT operand to its packed uint16.
        Specials may emit a (selector, count) pair or a pre-merged int;
        the merge helpers accept and validate both. Runs
        unconditionally, independent of the optional convert_fast
        lowering (which only relabels VAR->FAST and leaves non-VAR
        fields, including merged OPCOUNT/CMPCOUNT args, untouched).
        """

        result = []
        for op, arg in self.instructions:
            if op.style == OpStyle.OPCOUNT:
                arg = merge_opcount(arg)
            elif op.style == OpStyle.CMPCOUNT:
                arg = merge_cmpcount(arg)
            elif OpStyle.OPCOUNT in op.extra:
                arg = tuple(
                    merge_opcount(v) if fk == OpStyle.OPCOUNT else v
                    for v, fk in zip(arg, op.extra))
            result.append((op, arg))
        self.instructions = result


    def convert_fast(self, base_nspec):
        """
        Lower VAR-family rows to their FAST twins where the name has a
        fast slot in the active namespace scope, tracking PUSH_NS /
        PUSH_COLLECT scope entries through the stream
        """

        from .compiler import CompilerMode

        result = []
        nstack = []
        findex = []

        def reload_findex():
            findex.clear()
            if nstack:
                n = nstack[-1]
                findex.extend(n.local_syms)
                findex.extend(n.closure_syms)
                findex.extend(n.global_syms)

        def push_nspec(n):
            nstack.append(n)
            reload_findex()

        def pop_nspec():
            nstack.pop()
            reload_findex()

        if self.mode == CompilerMode.FUNCTION:
            push_nspec(base_nspec)

        def fast_slot(name):
            # Return the fast slot index for name, or -1 if it has no
            # fast slot (not in the pool, or pool is at capacity).
            try:
                idx = findex.index(name)
            except ValueError:
                return -1
            return idx if idx < NAMESPACE_FAST_SLOT_MAX else -1

        nspec = None
        for op, arg in self.instructions:
            if op == OpCode.CONST:
                if isinstance(arg, NamespaceSpec):
                    nspec = arg

            elif op == OpCode.PUSH_NS:
                push_nspec(arg[0])
                nspec = None

            elif op == OpCode.PUSH_COLLECT:
                push_nspec(arg if isinstance(arg, NamespaceSpec) else nspec)
                nspec = None

            elif op in (OpCode.POP_NS, OpCode.POP_COLLECT):
                pop_nspec()

            elif nstack and op == OpCode.CHECK_COMPARE_VAR:
                # CMPSRC row: slot kinds are data-dependent (a set source
                # bit means CONST, else the declared VAR kind), so the
                # generic schema-driven lowering below cannot apply. Lower
                # each name-sourced slot to its fast index; all-or-nothing
                # as usual, const-bit slots pass through untouched.
                cmpsrc, x, y, label = arg
                nx = x if (cmpsrc & CMPSRC_X_CONST) else fast_slot(x)
                ny = y if (cmpsrc & CMPSRC_Y_CONST) else fast_slot(y)
                if (((cmpsrc & CMPSRC_X_CONST) or nx >= 0)
                        and ((cmpsrc & CMPSRC_Y_CONST) or ny >= 0)):
                    op = OpCode.CHECK_COMPARE_FAST
                    arg = (cmpsrc, nx, ny, label)

            elif nstack and op in VAR_TO_FAST:
                # An EXTRA opcode carries its schema tuple in op.extra
                # (opcodes.incl field kinds). Zip with the operand
                # 4-tuple; each VAR-kind slot is lowered to a fast index
                # if one is available; otherwise that field stays as the
                # original value and the whole opcode stays as VAR.
                new_op = VAR_TO_FAST[op]
                if op.extra:
                    new_arg = tuple([
                        fast_slot(v) if fk == OpStyle.VAR else v
                        for v, fk in zip(arg, op.extra)])
                    if all(
                            idx >= 0
                            for v, fk, idx in zip(arg, op.extra, new_arg)
                            if fk == OpStyle.VAR):
                        arg = new_arg
                        op = new_op
                else:
                    idx = fast_slot(arg)
                    if idx >= 0:
                        arg = idx
                        op = new_op

            result.append((op, arg))

        self.instructions = result


    def promote_tail_calls(self):
        """
        Rewrite calls with a clear route to RETURN 1 into their
        TAIL_CALL forms.

        One linear read maintains the extent stack from the push and
        pop rows. At a promotable call, promotion is denied if any
        open extent is not discardable -- an extent whose uninstall
        behavior must run after the call cannot be tail-called
        through. Otherwise the route reads forward: extent pops,
        LABEL, POSITION, and NOOP rows pass; unconditional JUMPs are
        followed through the label index with a seen set guarding
        cycles; arrival at RETURN 1 promotes, and any other arrival
        invalidates. Yielding streams are skipped outright: generator
        frames never use tail-call frame reuse. Every call op has a
        schema-identical TAIL twin, so promotion is a pure relabel
        through the name-derived map -- fused VAR, FAST, and ATTR
        forms included.
        """

        instr = self.instructions
        yield_ops = (OpCode.YIELD, OpCode.YIELD_FROM)
        if any(op in yield_ops for op, _ in instr):
            return

        labels = {
            arg: i for i, (op, arg) in enumerate(instr)
            if op == OpCode.LABEL}

        def clear_route(start):
            seen = set()
            i = start
            while i < len(instr):
                op, arg = instr[i]
                if op == OpCode.RETURN:
                    return arg == 1
                if op == OpCode.JUMP:
                    if arg in seen or arg not in labels:
                        return False
                    seen.add(arg)
                    i = labels[arg]
                    continue
                if op in ROUTE_TRANSPARENT:
                    i += 1
                    continue
                return False
            return False

        result = []
        extents = []
        for i, (op, arg) in enumerate(instr):
            discard = EXTENT_DISCARDABLE.get(op)
            if discard is not None:
                extents.append(discard)
            elif op in EXTENT_POPS:
                if extents:
                    extents.pop()

            elif (op in CALL_TO_TAIL_CALL
                  and all(extents) and clear_route(i + 1)):
                op = CALL_TO_TAIL_CALL[op]

            result.append((op, arg))

        self.instructions = result


    def demote_tail_calls_if_yields(self):
        """
        If the stream contains yield or yield-from, replace TAIL_CALL*
        with CALL* so generator frames never use tail-call frame reuse.
        Native Code lowering rejects the combination outright.
        """

        instr = self.instructions
        yield_ops = (OpCode.YIELD, OpCode.YIELD_FROM)
        if not any(op in yield_ops for op, _ in instr):
            return
        self.instructions = [
            (TAIL_CALL_TO_CALL.get(op, op), arg)
            for op, arg in instr]


    def process(self, resolve_nspec):
        """
        Run the full gated pipeline and return the finished rows.

        The namespace spec is not knowable until the compressed stream
        has been scanned for inner code (closure and write-request
        merging), so the caller supplies resolve_nspec: a callable
        receiving the compressed rows and returning the base
        NamespaceSpec for the fast lowering.

        :param resolve_nspec: callable(rows) -> NamespaceSpec
        :return: the finished instruction rows
        """

        self.compress()

        # promote and demote are brothers, dispatched exclusively: a
        # yielding stream never promotes and instead demotes any raw
        # TAIL_CALL*; every other stream may promote under the tco gate.
        if any(op in (OpCode.YIELD, OpCode.YIELD_FROM)
               for op, _ in self.instructions):
            self.demote_tail_calls_if_yields()
        elif self.gates.get('tco'):
            self.promote_tail_calls()

        self.normalize_opcount()

        base_nspec = resolve_nspec(self.instructions)

        if self.gates.get('convert-fast', True):
            self.convert_fast(base_nspec)

        return self.instructions


# The end.
