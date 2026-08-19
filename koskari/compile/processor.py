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


from ..lib import (Compare, NAMESPACE_FAST_SLOT_MAX, NamespaceSpec,
                   OpCode, Operand, OpStyle)


OPERAND_COUNT = len(Operand)


def merge_opcount(value):
    """
    Normalize an OPCOUNT-style operand to its merged uint16 (operator selector
    in the high byte, reduce count in the low byte).

    Accepts either idiom, so specials may emit either:
      - a ``(op_type, count)`` pair, which is merged, or
      - a pre-merged int, which is validated to have been correctly packed.

    Both are range- and op-type-checked, so no soften-side reversal is needed.

    :param value: (op_type, count) pair, or a pre-merged int
    :return: merged uint16 int
    :raises ValueError: on a bad shape, out-of-range byte, or unknown op-type
    """

    if isinstance(value, tuple):
        if len(value) != 2:
            raise ValueError(f"opcount operand must be (op_type, count), "
                             f"got {value!r}")
        op_type, count = value
        op_type = int(getattr(op_type, 'value', op_type))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError(f"opcount int out of uint16 range: {value!r}")
        op_type, count = merged >> 8, merged & 0xff

    if not 0 <= op_type < OPERAND_COUNT:
        raise ValueError(f"opcount op-type is not a known Operand: {op_type!r}")
    if not 0 <= count <= 0xff:
        raise ValueError(f"opcount count out of byte range: {count!r}")

    return (op_type << 8) | count


COMPARE_COUNT = len(Compare)


def merge_cmpcount(value):
    """
    Normalize a CMPCOUNT-style operand to its merged uint16 (Compare
    selector in the high byte, reduce count in the low byte). The
    CMPCOUNT twin of merge_opcount: accepts a (selector, count) pair or
    a pre-merged int, both range- and selector-checked.

    :param value: (selector, count) pair, or a pre-merged int
    :return: merged uint16 int
    :raises ValueError: on a bad shape, out-of-range byte, or unknown selector
    """

    if isinstance(value, tuple):
        if len(value) != 2:
            raise ValueError(f"cmpcount operand must be (selector, count), "
                             f"got {value!r}")
        sel, count = value
        sel = int(getattr(sel, 'value', sel))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError(f"cmpcount int out of uint16 range: {value!r}")
        sel, count = merged >> 8, merged & 0xff

    if not 0 <= sel < COMPARE_COUNT:
        raise ValueError(f"cmpcount selector is not a known Compare: {sel!r}")
    if not 0 <= count <= 0xff:
        raise ValueError(f"cmpcount count out of byte range: {count!r}")

    return (sel << 8) | count


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


# The squish rules' prior-row match sets, precomputed as frozensets for
# the seekback. The JUMP_FALSE set unions its three disjoint prior
# kinds; the rule dispatches on which one matched.
JUMP_PRIORS = frozenset((OpCode.JUMP_TRUE, OpCode.JUMP_FALSE))
JUMP_FALSE_PRIORS = frozenset(
    (OpCode.CONST, OpCode.OBJECT_COMPARE)) | frozenset(CHECK_SQUASHES)
SWITCH_PRIORS = frozenset(SWITCH_SQUASHES)
POP_PRIORS = frozenset((OpCode.CONST, OpCode.DUP))
CONST_PRIOR = frozenset((OpCode.CONST,))
JUMP_PRIOR = frozenset((OpCode.JUMP,))


def build_var_to_fast():
    # Pre-compiled map: VAR opcode -> FAST opcode for convert_fast lowering.
    # Built by finding each FAST_* opcode and resolving to a matching VAR_*
    # opcode.

    var_to_fast = {}
    for opcode in OpCode:
        opname = opcode.name
        if not (opname.startswith('FAST')
                or opname.startswith('CALL_FAST')):
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


TAIL_CALL_TO_CALL = {
    OpCode.TAIL_CALL: OpCode.CALL,
    OpCode.TAIL_CALL_NAMES: OpCode.CALL_NAMES,
    OpCode.TAIL_CALL_STAR: OpCode.CALL_STAR,
}


# A rule returns DONE when it fully handled its trigger row (spliced a
# replacement, or edited prior rows and dropped the trigger).
DONE = object()


def rules_registry():
    """
    Build a pass's rule registry: the dict of trigger opcode to rule
    method, paired with the decorator that registers methods into it
    """

    table = {}

    def register(*ops):
        def deco(fn):
            for op in ops:
                table[op] = fn
            return fn
        return deco

    return table, register


# The per-pass rule registries: trigger opcode -> rule method. Sight
# reading a pass means reading its registered rules in source order.
SQUISH_RULES, squish_rule = rules_registry()
FUSE_RULES, fuse_rule = rules_registry()


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
        # stream; the seekback and splice services address it
        self.rows = None


    def seek_back(self, want, accept, start=None):
        """
        Scan the working rows backward, collecting up to want indices
        whose op is in accept. Rows whose op is OpCode.POSITION are
        stepped over and their indices gathered separately; any other
        op ends the scan.

        :param want: how many accepted rows to find
        :param accept: ops that match
        :param start: scan begins just below this index; default is
            the end of the working rows
        :return: (found, skipped) where found lists the matching row
            indices nearest-first, or None if fewer than want matched,
            and skipped lists the stepped-over rows' indices
        """

        posop = OpCode.POSITION
        rows = self.rows
        found = []
        skipped = []
        i = (len(rows) if start is None else start) - 1
        while i >= 0 and len(found) < want:
            rop = rows[i][0]
            if rop == posop:
                skipped.append(i)
            elif rop in accept:
                found.append(i)
            else:
                break
            i -= 1
        return (found if len(found) == want else None), skipped


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


    FEEDERS = frozenset((OpCode.VAR, OpCode.CONST))

    def scan_feeders(self, want):
        """
        Seek back over contiguous bare VAR / CONST feeder rows
        """

        return self.seek_back(want, self.FEEDERS)


    def ternary_shape(self, found):
        """
        The bounds shape: CONST below, VAR middle, CONST on top
        """

        rows = self.rows
        return (rows[found[2]][0] == OpCode.CONST
                and rows[found[1]][0] == OpCode.VAR
                and rows[found[0]][0] == OpCode.CONST)


    def apply_rules(self, rules, rows):
        """
        Walk the incoming instructions, dispatching each row through
        the pass's rule table into the working list.

        A rule returns DONE when it fully handled its trigger, a new
        (op, arg) pair to retry dispatch on a rewritten trigger -- the
        lowering ladder -- or None to decline; an undispatched or
        declined row is appended unchanged. Retry termination is the
        rule author's obligation: every rewrite must consume rows or
        produce an op that resolves without further rewriting.
        """

        self.rows = rows
        for op, arg in self.instructions:
            while True:
                rule = rules.get(op)
                if rule is None:
                    rows.append((op, arg))
                    break
                outcome = rule(self, op, arg)
                if outcome is None:
                    rows.append((op, arg))
                    break
                if outcome is DONE:
                    break
                op, arg = outcome
        self.rows = None
        return rows


    @squish_rule(OpCode.POP)
    def pop_pushed_value(self, op, arg):
        """
        POP 1 of a value just pushed by CONST or DUP: both rows vanish
        """

        if arg != 1:
            return None
        found, skip = self.seek_back(1, POP_PRIORS)
        if not found:
            return None
        self.splice(found[0], skip, [])
        return DONE


    @squish_rule(OpCode.OBJECT_NOT)
    def not_const_fold(self, op, arg):
        """
        OBJECT_NOT of a constant folds into the negated constant
        """

        found, skip = self.seek_back(1, CONST_PRIOR)
        if not found:
            return None
        rows = self.rows
        rows[found[0]] = (OpCode.CONST, not rows[found[0]][1])
        return DONE


    @squish_rule(OpCode.JUMP_TRUE)
    def jump_true_const_fold(self, op, arg):
        """
        JUMP_TRUE on a constant resolves to a plain JUMP or nothing
        """

        found, skip = self.seek_back(1, CONST_PRIOR)
        if not found:
            return None
        rep = [(OpCode.JUMP, arg)] if self.rows[found[0]][1] else []
        self.splice(found[0], skip, rep)
        return DONE


    @squish_rule(OpCode.JUMP_FALSE)
    def jump_false_prior_fold(self, op, arg):
        """
        JUMP_FALSE resolves against its prior: a constant folds to a
        plain JUMP or nothing, a check-squashable predicate combines
        into its single check jump, and OBJECT_COMPARE fuses into
        CHECK_COMPARE carrying selector, count, and target in one row
        """

        found, skip = self.seek_back(1, JUMP_FALSE_PRIORS)
        if not found:
            return None
        prior, prior_arg = self.rows[found[0]]

        if prior == OpCode.CONST:
            rep = [] if prior_arg else [(OpCode.JUMP, arg)]
            self.splice(found[0], skip, rep)

        elif prior in CHECK_SQUASHES:
            self.splice(found[0], skip, [(CHECK_SQUASHES[prior], arg)])

        else:
            sel, cnt = split_cmpcount(prior_arg)
            self.splice(found[0], skip, [(
                OpCode.CHECK_COMPARE, (sel, cnt, arg, None))])

        return DONE


    @squish_rule(OpCode.SWITCH_EQ)
    def switch_eq_squash(self, op, arg):
        """
        SWITCH_EQ absorbs its CONST, VAR, or VAR_ATTR comparand into
        the corresponding single-op switch form
        """

        found, skip = self.seek_back(1, SWITCH_PRIORS)
        if not found:
            return None
        prior, pargs = self.rows[found[0]]
        if isinstance(pargs, tuple):
            new_arg = (arg, *pargs[:3])
        else:
            new_arg = (arg, pargs, None, None)
        self.splice(found[0], skip, [(SWITCH_SQUASHES[prior], new_arg)])
        return DONE


    @squish_rule(OpCode.LABEL)
    def label_jump_elision(self, op, arg):
        """
        A jump targeting the immediately following label is useless: a
        plain JUMP vanishes; a conditional jump vanishes with its
        constant test, or degrades to POP 1 when the test is dynamic.
        The label itself always survives via the declining return.
        """

        rows = self.rows
        found, skip = self.seek_back(1, JUMP_PRIOR)
        if found and rows[found[0]][1] == arg:
            self.splice(found[0], skip, [])
            return None

        found, skip = self.seek_back(1, JUMP_PRIORS)
        if found and rows[found[0]][1] == arg:
            below, skip2 = self.seek_back(1, CONST_PRIOR, start=found[0])
            if below:
                self.splice(below[0], skip + skip2, [])
            else:
                rows[found[0]] = (OpCode.POP, 1)
        return None


    @squish_rule(OpCode.RETURN, OpCode.BREAK,
                 OpCode.CONTINUE, OpCode.YIELD)
    def control_none_normalize(self, op, arg):
        """
        A control op carrying a just-pushed CONST None normalizes to
        its bare zero-arg form: CONST None, RETURN 1 -> RETURN 0
        """

        if arg != 1:
            return None
        found, skip = self.seek_back(1, CONST_PRIOR)
        if not found or self.rows[found[0]][1] is not None:
            return None
        self.splice(found[0], skip, [])
        return (op, 0)


    @squish_rule(OpCode.CAR, OpCode.CDR)
    def car_cdr_merge(self, op, arg):
        """
        Consecutive CAR or consecutive CDR rows merge their counts
        """

        found, skip = self.seek_back(1, frozenset((op,)))
        if not found:
            return None
        rows = self.rows
        prev_op, prev_arg = rows[found[0]]
        rows[found[0]] = (prev_op, prev_arg + arg)
        return DONE


    @squish_rule(OpCode.VAR_STORE)
    def var_store_const_fuse(self, op, arg):
        """
        VAR_STORE of a just-pushed constant fuses to the stack-free
        VAR_STORE_CONST row
        """

        found, skip = self.seek_back(1, CONST_PRIOR)
        if not found:
            return None
        prior_arg = self.rows[found[0]][1]
        self.splice(found[0], skip, [(
            OpCode.VAR_STORE_CONST, (arg, prior_arg, None, None))])
        return DONE


    @squish_rule(OpCode.VAR_INPLACE)
    def var_inplace_const_fuse(self, op, arg):
        """
        Single-operand VAR_INPLACE with a constant rhs fuses to the
        stack-free VAR_INPLACE_CONST: bare operator selector, name,
        pooled operand
        """

        found, skip = self.seek_back(1, CONST_PRIOR)
        if not found:
            return None
        merged = merge_opcount(arg[0])
        if merged & 0xff != 1:
            return None
        prior_arg = self.rows[found[0]][1]
        self.splice(found[0], skip, [(
            OpCode.VAR_INPLACE_CONST,
            (merged >> 8, arg[1], prior_arg, None))])
        return DONE


    @fuse_rule(OpCode.CHECK_COMPARE)
    def fuse_check_compare(self, op, arg):
        """
        Arity-2 CHECK_COMPARE absorbs its bare feeders (not both CONST
        -- that is a folding case) into CHECK_COMPARE_VAR with a CMPSRC
        field carrying the selector and per-operand source bits. The
        arity-3 CONST / VAR / CONST bounds shape cannot also carry the
        label in one row, so it splits into the bool-producing
        OBJECT_COMPARE_VAR plus JUMP_FALSE.
        """

        rows = self.rows
        if arg[1] == 2:
            found, skip = self.scan_feeders(2)
            if found:
                y_op, y_arg = rows[found[0]]
                x_op, x_arg = rows[found[1]]
                x_const = x_op == OpCode.CONST
                y_const = y_op == OpCode.CONST
                if not (x_const and y_const):
                    sel, _cnt, label, _pad = arg
                    self.splice(found[1], skip, [(
                        OpCode.CHECK_COMPARE_VAR,
                        (make_cmpsrc(sel, x_const, y_const),
                         x_arg, y_arg, label))])
                    return DONE

        elif arg[1] == 3:
            found, skip = self.scan_feeders(3)
            if found and self.ternary_shape(found):
                sel, _cnt, label, _pad = arg
                self.splice(found[2], skip, [
                    (OpCode.OBJECT_COMPARE_VAR,
                     (sel, rows[found[2]][1], rows[found[1]][1],
                      rows[found[0]][1])),
                    (OpCode.JUMP_FALSE, label),
                ])
                return DONE

        return None


    @fuse_rule(OpCode.OBJECT_COMPARE)
    def fuse_compare_bounds(self, op, arg):
        """
        The arity-3 CONST / VAR / CONST bounds shape in value position
        collapses into the bool-producing OBJECT_COMPARE_VAR
        """

        sel, cnt = split_cmpcount(arg)
        if cnt != 3:
            return None
        found, skip = self.scan_feeders(3)
        if not (found and self.ternary_shape(found)):
            return None
        rows = self.rows
        self.splice(found[2], skip, [(
            OpCode.OBJECT_COMPARE_VAR,
            (sel, rows[found[2]][1], rows[found[1]][1],
             rows[found[0]][1]))])
        return DONE


    @fuse_rule(OpCode.OBJECT_ITER)
    def fuse_iter(self, op, arg):
        """
        OBJECT_ITER fed by a bare VAR collapses into VAR_ITER
        """

        found, skip = self.scan_feeders(1)
        if not (found and self.rows[found[0]][0] == OpCode.VAR):
            return None
        self.splice(found[0], skip, [(
            OpCode.VAR_ITER, self.rows[found[0]][1])])
        return DONE


    @fuse_rule(OpCode.OBJECT_NEXT)
    def fuse_next(self, op, arg):
        """
        OBJECT_NEXT fed by a bare VAR iterator collapses into VAR_NEXT.
        The 2-arg form fuses only when the default is also a bare
        feeder (absorbing the iterator load moves its name resolution
        after the default's row); the default row stays on the stack
        and VAR_NEXT pops it.
        """

        rows = self.rows
        if arg == 1:
            found, skip = self.scan_feeders(1)
            if found and rows[found[0]][0] == OpCode.VAR:
                self.splice(found[0], skip, [(
                    OpCode.VAR_NEXT,
                    (rows[found[0]][1], 1, None, None))])
                return DONE
        else:
            found, skip = self.scan_feeders(2)
            if found and rows[found[1]][0] == OpCode.VAR:
                default_row = rows[found[0]]
                it_name = rows[found[1]][1]
                self.splice(found[1], skip, [
                    default_row,
                    (OpCode.VAR_NEXT, (it_name, 2, None, None)),
                ])
                return DONE
        return None


    @fuse_rule(OpCode.OBJECT_FORMAT)
    def fuse_format(self, op, arg):
        """
        OBJECT_FORMAT (arg=1) fed by a bare VAR collapses into
        VAR_FORMAT. The 2-arg form with a constant rules feeder demotes
        to OBJECT_FORMAT_CONST and retries into the format lowering
        ladder; with dynamic rules it stays unfused, since absorbing
        the object load would move its name resolution past the rules
        evaluation.
        """

        rows = self.rows
        if arg == 1:
            found, skip = self.scan_feeders(1)
            if found and rows[found[0]][0] == OpCode.VAR:
                self.splice(found[0], skip, [(
                    OpCode.VAR_FORMAT, rows[found[0]][1])])
                return DONE
            return None

        found, skip = self.scan_feeders(1)
        if not (found and rows[found[0]][0] == OpCode.CONST):
            return None
        rules = rows[found[0]][1]
        self.splice(found[0], skip, [])
        return (OpCode.OBJECT_FORMAT_CONST, rules)


    @fuse_rule(OpCode.OBJECT_FORMAT_CONST)
    def fuse_format_const(self, op, arg):
        """
        OBJECT_FORMAT_CONST fed by a bare VAR collapses into
        VAR_FORMAT_CONST
        """

        found, skip = self.scan_feeders(1)
        if not (found and self.rows[found[0]][0] == OpCode.VAR):
            return None
        self.splice(found[0], skip, [(
            OpCode.VAR_FORMAT_CONST,
            (self.rows[found[0]][1], arg, None, None))])
        return DONE


    def squish(self):
        """
        The peephole half of the compress pipeline: the squish rule set
        over the shared driver, plus trailing RETURN normalization.
        Feeder fusion and VAR run packing follow as separate passes.
        """

        # the NOOP sentinel floors every seekback, so rules never need
        # an emptiness check; it is stripped at the end
        rows = self.apply_rules(SQUISH_RULES, [(OpCode.NOOP, 0)])

        if rows[-1][0] != OpCode.RETURN:
            if rows[-1][0] == OpCode.CONST \
               and rows[-1][1] is None:
                rows[-1] = (OpCode.RETURN, 0)
            else:
                rows.append((OpCode.RETURN, 1))

        if len(rows) > 1:
            # if we actually had ops, we'll have a leading NOOP that we
            # can remove
            rows = rows[1:]

        self.instructions = rows


    def fuse_check_feeders(self):
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
        self.fuse_check_feeders()
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

        def push_nspec(n):
            nstack.append(n)
            findex.clear()
            findex.extend(n.local_syms)
            findex.extend(n.closure_syms)
            findex.extend(n.global_syms)

        def pop_nspec():
            nstack.pop()
            findex.clear()
            if nstack:
                n = nstack[-1]
                findex.extend(n.local_syms)
                findex.extend(n.closure_syms)
                findex.extend(n.global_syms)

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
        self.normalize_opcount()

        base_nspec = resolve_nspec(self.instructions)

        if self.gates.get('convert-fast', True):
            self.convert_fast(base_nspec)

        self.demote_tail_calls_if_yields()

        return self.instructions


# The end.
