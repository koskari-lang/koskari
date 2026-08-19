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

The bytecode processor: a use-and-discard machine that internalizes an
instruction stream and applies the gated transformation pipeline --
peephole squish, feeder fusion, VAR run packing, operand normalization,
VAR to FAST lowering, and generator tail-call demotion -- yielding the
finished rows for Code construction

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from ..lib import (Compare, NAMESPACE_FAST_SLOT_MAX, NamespaceSpec,
                   OpCode, Operand, OpStyle)


_OPERAND_COUNT = len(Operand)


def merge_opcount(value):
    """
    Normalize an OPCOUNT-style operand to its merged uint16 (operator selector
    in the high byte, reduce count in the low byte).

    Accepts either idiom, so specials may emit whichever reads cleanly:
      - a ``(op_type, count)`` pair, which is merged, or
      - a pre-merged int, which is validated to have been correctly packed.

    Both are range- and op-type-checked, so no soften-side reversal is needed.

    :param value: (op_type, count) pair, or a pre-merged int
    :return: merged uint16 int
    :raises ValueError: on a bad shape, out-of-range byte, or unknown op-type
    """

    if isinstance(value, tuple):
        if len(value) != 2:
            raise ValueError("opcount operand must be (op_type, count), "
                             "got %r" % (value,))
        op_type, count = value
        op_type = int(getattr(op_type, 'value', op_type))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError("opcount int out of uint16 range: %r" % (value,))
        op_type, count = merged >> 8, merged & 0xff

    if not 0 <= op_type < _OPERAND_COUNT:
        raise ValueError("opcount op-type is not a known Operand: %r" % (op_type,))
    if not 0 <= count <= 0xff:
        raise ValueError("opcount count out of byte range: %r" % (count,))

    return (op_type << 8) | count


_COMPARE_COUNT = len(Compare)


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
            raise ValueError("cmpcount operand must be (selector, count), "
                             "got %r" % (value,))
        sel, count = value
        sel = int(getattr(sel, 'value', sel))
        count = int(count)
    else:
        merged = int(value)
        if not 0 <= merged <= 0xffff:
            raise ValueError("cmpcount int out of uint16 range: %r" % (value,))
        sel, count = merged >> 8, merged & 0xff

    if not 0 <= sel < _COMPARE_COUNT:
        raise ValueError("cmpcount selector is not a known Compare: %r" % (sel,))
    if not 0 <= count <= 0xff:
        raise ValueError("cmpcount count out of byte range: %r" % (count,))

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


_CHECK_SQUASHES = {
    OpCode.OBJECT_NOT: OpCode.JUMP_TRUE,
    OpCode.EXCEPTION_MATCH: OpCode.CHECK_EXCEPTION,
    OpCode.NILP: OpCode.CHECK_NIL,
}


_SWITCH_SQUASHES = {
    OpCode.CONST: OpCode.SWITCH_EQ_CONST,
    OpCode.VAR: OpCode.SWITCH_EQ_VAR,
    OpCode.VAR_ATTR: OpCode.SWITCH_EQ_VAR_ATTR,
}


# The squish rules' prior-row match sets, precomputed as frozensets for
# the seekback. The JUMP_FALSE set unions its three disjoint prior
# kinds; the rule dispatches on which one matched.
_JUMP_PRIORS = frozenset((OpCode.JUMP_TRUE, OpCode.JUMP_FALSE))
_JUMP_FALSE_PRIORS = frozenset(
    (OpCode.CONST, OpCode.OBJECT_COMPARE)) | frozenset(_CHECK_SQUASHES)
_SWITCH_PRIORS = frozenset(_SWITCH_SQUASHES)
_POP_PRIORS = frozenset((OpCode.CONST, OpCode.DUP))
_CONST_PRIOR = frozenset((OpCode.CONST,))
_JUMP_PRIOR = frozenset((OpCode.JUMP,))


def _build_var_to_fast():
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


VAR_TO_FAST = _build_var_to_fast()


_TAIL_CALL_TO_CALL = {
    OpCode.TAIL_CALL: OpCode.CALL,
    OpCode.TAIL_CALL_NAMES: OpCode.CALL_NAMES,
    OpCode.TAIL_CALL_STAR: OpCode.CALL_STAR,
}


def normalize_opcount(instr):
    """
    Merge every OPCOUNT and CMPCOUNT operand to its
    packed uint16. Specials may emit a (selector, count) pair or a
    pre-merged int; the merge helpers accept and validate both. Runs
    unconditionally, independent of the optional convert_fast lowering
    (which only relabels VAR->FAST and leaves non-VAR fields, including
    merged OPCOUNT/CMPCOUNT args, untouched).
    """

    result = []
    for op, arg in instr:
        if op.style == OpStyle.OPCOUNT:
            arg = merge_opcount(arg)
        elif op.style == OpStyle.CMPCOUNT:
            arg = merge_cmpcount(arg)
        elif OpStyle.OPCOUNT in op.extra:
            arg = tuple(
                merge_opcount(v) if fk == OpStyle.OPCOUNT else v
                for v, fk in zip(arg, op.extra))
        result.append((op, arg))
    return result


def convert_fast(base_nspec, instr, mode):
    """
    Lower VAR-family rows to their FAST twins where the name has a fast
    slot in the active namespace scope, tracking PUSH_NS / PUSH_COLLECT
    scope entries through the stream
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

    if mode == CompilerMode.FUNCTION:
        push_nspec(base_nspec)

    def fast_slot(name):
        # Return the fast slot index for name, or -1 if it has no fast
        # slot (not in the pool, or pool is at capacity).
        try:
            idx = findex.index(name)
        except ValueError:
            return -1
        return idx if idx < NAMESPACE_FAST_SLOT_MAX else -1

    nspec = None
    for op, arg in instr:
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
            # (opcodes.incl field kinds). Zip with the operand 4-tuple; each
            # VAR-kind slot is lowered to a fast index if one is
            # available; otherwise that field stays as the original value
            # and the whole opcode stays as VAR.
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

    return result


def demote_tail_calls_if_yields(instr):
    """
    If the stream contains yield or yield-from, replace TAIL_CALL* with
    CALL* so generator frames never use tail-call frame reuse. Native
    Code lowering rejects the combination outright.
    """

    yield_ops = (OpCode.YIELD, OpCode.YIELD_FROM)
    if not any(op in yield_ops for op, _ in instr):
        return instr
    return [
        (_TAIL_CALL_TO_CALL.get(op, op), arg)
        for op, arg in instr]


def pack_var_runs(instr):
    """
    Late sweep: combine runs of consecutive bare VAR loads into VAR4 /
    VAR2 rows (greedy, in that order). Runs after every other squash so
    consumer-specific fusions get first claim on their feeder rows, and
    breaks at any non-VAR instruction (including LABEL). POSITION rows
    inside a run are absorbed -- the merged row keeps the position in
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

    for op, arg in instr:
        if op == OpCode.VAR:
            run.append(arg)
            continue
        if run and op == OpCode.POSITION:
            trailing_pos = (op, arg)
            continue
        flush()
        result.append((op, arg))

    flush()
    return result


def squish(instr):
    """
    Functional entry for the peephole squish pass: run it alone over
    instr through a throwaway processor
    """

    proc = ByteCodeProcessor(instr)
    proc.squish()
    return proc.instructions


def fuse_check_feeders(instr):
    """
    Functional entry for the feeder-absorption pass: run it alone over
    instr through a throwaway processor
    """

    proc = ByteCodeProcessor(instr)
    proc.fuse_check_feeders()
    return proc.instructions


class ByteCodeProcessor:
    """
    A single-use transformation machine over one instruction stream.

    Constructed per code object with the rows, the pragma-derived gate
    flags, and the compilation mode; mutated in place as passes apply;
    discarded after the finished rows are taken. Rules and pass tables
    are stateless module-level data -- the instance carries only the
    stream and its per-stream working state, so its lifetime is exactly
    the validity of that state.
    """

    # Ops the backward scans treat as invisible. The literal-lookback
    # peephole rules pass an empty set instead; flipping them onto this
    # default is a deliberate, separately-verified emission change.
    TRANSPARENT = frozenset((OpCode.POSITION,))

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
        self._rows = None


    def _seek_back(self, want, accept, transparent=(), start=None):
        """
        Scan the working rows backward, collecting up to want indices
        whose op is in accept. Rows whose op is in transparent are
        stepped over and their indices gathered separately; any other
        op ends the scan.

        :param want: how many accepted rows to find
        :param accept: ops that match
        :param transparent: ops stepped over without matching
        :param start: scan begins just below this index; default is
            the end of the working rows
        :return: (found, skipped) where found lists the matching row
            indices nearest-first, or None if fewer than want matched,
            and skipped lists the stepped-over rows' indices
        """

        rows = self._rows
        found = []
        skipped = []
        i = (len(rows) if start is None else start) - 1
        while i >= 0 and len(found) < want:
            rop = rows[i][0]
            if rop in transparent:
                skipped.append(i)
            elif rop in accept:
                found.append(i)
            else:
                break
            i -= 1
        return (found if len(found) == want else None), skipped


    def _splice(self, start, skipped, replacement):
        """
        Replace the working rows from start onward with replacement,
        re-emitting the latest stepped-over row (by index) afterward so
        following instructions keep their position
        """

        rows = self._rows
        trailing = rows[max(skipped)] if skipped else None
        del rows[start:]
        rows.extend(replacement)
        if trailing is not None:
            rows.append(trailing)


    def squish(self):
        """
        The peephole half of the compress pipeline: single-lookback
        folds and fusions plus trailing RETURN normalization. Feeder
        fusion and VAR run packing follow as separate passes.

        The rules seek their prior row with an empty transparency set,
        preserving the literal-lookback behavior; the LABEL boundary is
        enforced by the seekback's break-on-other scan.
        """

        # the NOOP sentinel floors every seekback, so rules never need
        # an emptiness check; it is stripped at the end
        self._rows = squished = [(OpCode.NOOP, 0)]

        for (op, arg) in self.instructions:
            if op == OpCode.POP and arg == 1:
                found, _skip = self._seek_back(1, _POP_PRIORS)
                if found:
                    del squished[found[0]]
                    continue

            elif op == OpCode.OBJECT_NOT:
                found, _skip = self._seek_back(1, _CONST_PRIOR)
                if found:
                    val = squished[found[0]][1]
                    squished[found[0]] = (OpCode.CONST, not val)
                    continue

            elif op == OpCode.JUMP_TRUE:
                found, _skip = self._seek_back(1, _CONST_PRIOR)
                if found:
                    truthy = bool(squished[found[0]][1])
                    del squished[found[0]]
                    if truthy:
                        op = OpCode.JUMP
                    else:
                        continue

            elif op == OpCode.JUMP_FALSE:
                found, _skip = self._seek_back(1, _JUMP_FALSE_PRIORS)
                if found:
                    prior, prior_arg = squished[found[0]]

                    if prior == OpCode.CONST:
                        # jumping based on a constant value
                        truthy = not prior_arg
                        del squished[found[0]]
                        if truthy:
                            op = OpCode.JUMP
                        else:
                            continue

                    elif prior in _CHECK_SQUASHES:
                        # here we're combining an object operand with a
                        # jump into a single check jump
                        del squished[found[0]]
                        op = _CHECK_SQUASHES[prior]

                    else:
                        # fuse the compare with its jump: CHECK_COMPARE
                        # branches on chain failure, carrying the
                        # selector, count, and target in one EXTRA row
                        del squished[found[0]]
                        sel, cnt = split_cmpcount(prior_arg)
                        op = OpCode.CHECK_COMPARE
                        arg = (sel, cnt, arg, None)

            elif op == OpCode.SWITCH_EQ:
                found, _skip = self._seek_back(1, _SWITCH_PRIORS)
                if found:
                    prior, pargs = squished[found[0]]
                    op = _SWITCH_SQUASHES[prior]
                    if isinstance(pargs, tuple):
                        arg = (arg, *pargs[:3])
                    else:
                        arg = (arg, pargs, None, None)
                    del squished[found[0]]

            elif op == OpCode.LABEL:
                # omit useless jump when the jump target is the next
                # instruction: JUMP foo, LABEL foo -> NOOP
                found, _skip = self._seek_back(1, _JUMP_PRIOR)
                if found and squished[found[0]][1] == arg:
                    del squished[found[0]]
                else:
                    found, _skip = self._seek_back(1, _JUMP_PRIORS)
                    if found and squished[found[0]][1] == arg:
                        below, _skip = self._seek_back(
                            1, _CONST_PRIOR, start=found[0])
                        if below:
                            del squished[found[0]]
                            del squished[below[0]]
                        else:
                            squished[found[0]] = (OpCode.POP, 1)

            elif (op in (OpCode.RETURN,
                         OpCode.BREAK,
                         OpCode.CONTINUE,
                         OpCode.YIELD)
                  and arg == 1):
                found, _skip = self._seek_back(1, _CONST_PRIOR)
                if found and squished[found[0]][1] is None:
                    # CONST None, RETURN 1 -> RETURN 0
                    del squished[found[0]]
                    arg = 0

            elif op in (OpCode.CAR, OpCode.CDR):
                found, _skip = self._seek_back(1, frozenset((op,)))
                if found:
                    prev_op, prev_arg = squished[found[0]]
                    squished[found[0]] = (prev_op, prev_arg + arg)
                    continue

            elif op == OpCode.VAR_STORE:
                found, _skip = self._seek_back(1, _CONST_PRIOR)
                if found:
                    prior_arg = squished[found[0]][1]
                    del squished[found[0]]
                    op = OpCode.VAR_STORE_CONST
                    arg = (arg, prior_arg, None, None)

            elif op == OpCode.VAR_INPLACE:
                # CONST rhs on a single-operand augmented assignment
                # fuses to the stack-free form: bare operator selector,
                # name, pooled operand
                found, _skip = self._seek_back(1, _CONST_PRIOR)
                if found:
                    merged = merge_opcount(arg[0])
                    if merged & 0xff == 1:
                        prior_arg = squished[found[0]][1]
                        del squished[found[0]]
                        op = OpCode.VAR_INPLACE_CONST
                        arg = (merged >> 8, arg[1], prior_arg, None)

            squished.append((op, arg))

        if squished[-1][0] != OpCode.RETURN:
            if squished[-1][0] == OpCode.CONST \
               and squished[-1][1] is None:
                squished[-1] = (OpCode.RETURN, 0)
            else:
                squished.append((OpCode.RETURN, 1))

        if len(squished) > 1:
            # if we actually had ops, we'll have a leading NOOP that we
            # can remove
            squished = squished[1:]

        self.instructions = squished
        self._rows = None



    _FEEDERS = frozenset((OpCode.VAR, OpCode.CONST))

    def fuse_check_feeders(self):
        """
        Late sweep: absorb the operand feeders of comparison and
        iteration rows. All patterns require the immediately preceding
        value rows to be bare VAR or CONST loads (POSITION rows are
        transparent -- the fused row keeps the position in effect at the
        first feeder, and the latest interior position is re-emitted
        after the row):

        - arity-2 CHECK_COMPARE (not both feeders CONST -- that is a
          folding case): the trio collapses into CHECK_COMPARE_VAR with
          a CMPSRC field carrying the selector and per-operand source
          bits.
        - the ternary bounds shape CONST / VAR / CONST feeding an
          arity-3 comparison: collapses into the bool-producing
          OBJECT_COMPARE_VAR. In branch position (an arity-3
          CHECK_COMPARE) the row cannot also carry the label -- five
          fields -- so the check splits back into OBJECT_COMPARE_VAR
          plus JUMP_FALSE.
        - OBJECT_ITER fed by a bare VAR collapses into VAR_ITER.
        - OBJECT_NEXT fed by a bare VAR iterator collapses into
          VAR_NEXT. The 2-arg form fuses only when the default is also
          a bare feeder (absorbing the iterator load moves its name
          resolution after the default's row); the default row stays on
          the stack and VAR_NEXT pops it.
        - OBJECT_FORMAT (arg=1) or OBJECT_FORMAT_CONST fed by a bare
          VAR collapses into VAR_FORMAT / VAR_FORMAT_CONST. The 2-arg
          OBJECT_FORMAT stays unfused: there is no fused form carrying
          a stacked rules operand, and absorbing the object load would
          move its name resolution past the rules evaluation.

        The fast pass later lowers the VAR forms to their FAST twins.
        Runs before pack_var_runs so fusion gets first claim on the
        feeders.
        """

        self._rows = result = []

        def scan_back(want):
            return self._seek_back(
                want, self._FEEDERS, transparent=self.TRANSPARENT)

        def ternary_shape(found):
            # bounds shape: CONST below, VAR middle, CONST on top
            hi_op = result[found[0]][0]
            mid_op = result[found[1]][0]
            lo_op = result[found[2]][0]
            return (lo_op == OpCode.CONST
                    and mid_op == OpCode.VAR
                    and hi_op == OpCode.CONST)

        for op, arg in self.instructions:
            if op == OpCode.CHECK_COMPARE and arg[1] == 2:
                found, pos_rows = scan_back(2)
                if found:
                    y_op, y_arg = result[found[0]]
                    x_op, x_arg = result[found[1]]
                    x_const = x_op == OpCode.CONST
                    y_const = y_op == OpCode.CONST
                    if not (x_const and y_const):
                        sel, _cnt, label, _pad = arg
                        self._splice(found[1], pos_rows, [(
                            OpCode.CHECK_COMPARE_VAR,
                            (make_cmpsrc(sel, x_const, y_const),
                             x_arg, y_arg, label))])
                        continue

            elif op == OpCode.CHECK_COMPARE and arg[1] == 3:
                found, pos_rows = scan_back(3)
                if found and ternary_shape(found):
                    sel, _cnt, label, _pad = arg
                    lo = result[found[2]][1]
                    mid = result[found[1]][1]
                    hi = result[found[0]][1]
                    self._splice(found[2], pos_rows, [
                        (OpCode.OBJECT_COMPARE_VAR,
                         (sel, lo, mid, hi)),
                        (OpCode.JUMP_FALSE, label),
                    ])
                    continue

            elif op == OpCode.OBJECT_COMPARE:
                sel, cnt = split_cmpcount(arg)
                if cnt == 3:
                    found, pos_rows = scan_back(3)
                    if found and ternary_shape(found):
                        lo = result[found[2]][1]
                        mid = result[found[1]][1]
                        hi = result[found[0]][1]
                        self._splice(found[2], pos_rows, [(
                            OpCode.OBJECT_COMPARE_VAR,
                            (sel, lo, mid, hi))])
                        continue

            elif op == OpCode.OBJECT_ITER:
                found, pos_rows = scan_back(1)
                if found and result[found[0]][0] == OpCode.VAR:
                    self._splice(found[0], pos_rows, [(
                        OpCode.VAR_ITER, result[found[0]][1])])
                    continue

            elif op == OpCode.OBJECT_NEXT:
                if arg == 1:
                    found, pos_rows = scan_back(1)
                    if found and result[found[0]][0] == OpCode.VAR:
                        self._splice(found[0], pos_rows, [(
                            OpCode.VAR_NEXT,
                            (result[found[0]][1], 1, None, None))])
                        continue
                else:
                    # 2-arg: the iterator load sits below the default.
                    # Absorbing it moves the name resolution after the
                    # default's row, so the fusion requires the default
                    # to be a bare feeder too (scan_back guarantees it);
                    # the default row stays and VAR_NEXT pops it.
                    found, pos_rows = scan_back(2)
                    if found and result[found[1]][0] == OpCode.VAR:
                        default_row = result[found[0]]
                        it_name = result[found[1]][1]
                        self._splice(found[1], pos_rows, [
                            default_row,
                            (OpCode.VAR_NEXT,
                             (it_name, 2, None, None)),
                        ])
                        continue

            elif op == OpCode.OBJECT_FORMAT:
                if arg == 1:
                    found, pos_rows = scan_back(1)
                    if found and result[found[0]][0] == OpCode.VAR:
                        self._splice(found[0], pos_rows, [(
                            OpCode.VAR_FORMAT, result[found[0]][1])])
                        continue

            elif op == OpCode.OBJECT_FORMAT_CONST:
                found, pos_rows = scan_back(1)
                if found and result[found[0]][0] == OpCode.VAR:
                    self._splice(found[0], pos_rows, [(
                        OpCode.VAR_FORMAT_CONST,
                        (result[found[0]][1], arg, None, None))])
                    continue

            result.append((op, arg))

        self.instructions = result
        self._rows = None


    def compress(self):
        """
        The compress pipeline: peephole squish, then feeder fusion,
        then VAR run packing, in that order so fusion gets first claim
        on feeder rows
        """

        self.squish()
        self.fuse_check_feeders()
        self.instructions = pack_var_runs(self.instructions)


    def normalize_opcount(self):
        """
        Pack OPCOUNT and CMPCOUNT operands to their merged uint16 form
        """

        self.instructions = normalize_opcount(self.instructions)


    def convert_fast(self, base_nspec):
        """
        Lower VAR-family rows to their FAST twins against base_nspec
        """

        self.instructions = convert_fast(
            base_nspec, self.instructions, self.mode)


    def demote_tail_calls_if_yields(self):
        """
        Replace TAIL_CALL* with CALL* in yielding streams
        """

        self.instructions = demote_tail_calls_if_yields(self.instructions)


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
