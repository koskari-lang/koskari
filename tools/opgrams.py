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
opgrams -- opcode n-gram frequency analysis over compiled Koskari code.

Imports a group of modules (the bootstrap builtins, the churn benchmark
kernels), collects every reachable Code object, softens each back to its
symbolic instruction stream, and counts 2..4-opcode sequences within
straight-line runs. Sequences never cross a jump target (LABEL) and never
continue past a branching or terminal opcode, so every reported n-gram is
a candidate for opcode fusion. POSITION and NOOP rows are transparent.

Counts are reported both raw (emission frequency) and weighted by static
loop-nesting depth (weight = BASE ** depth), the latter approximating
execution frequency. A Markov successor table shows, per opcode, where
control usually flows next -- an opcode with one dominant successor is a
fusion candidate even at modest absolute counts.

Run from the repository root:

    tox -qe opgrams
    tox -qe opgrams -- --group churn --top 30
    tox -qe opgrams -- --group churn --dis

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import argparse
import importlib
import json
import sys

from collections import Counter, defaultdict
from os.path import abspath, dirname, join
from typing import Any, Dict, Iterator, List, Optional, Tuple


TOPDIR = dirname(dirname(abspath(__file__)))

MODULE_GROUPS = {
    'bootstrap': (
        'koskari.builtins',
    ),
    'cli': (
        'koskari.cli.launch',
        'koskari.cli.repl',
        'koskari.dis',
    ),
    'life': (
        'koslife.cli',
        'koslife.curse',
        'koslife.life',
    ),
    'churn': (
        'ki.binary_trees',
        'ki.branch',
        'ki.fannkuch',
        'ki.field',
        'ki.field_slots',
        'ki.list_each',
        'ki.loop',
        'ki.method',
        'ki.nbody',
        'ki.pidigits',
        'ki.starmap',
        'ki.tree',
        'ki.yieldfrom',
    ),
}

# Terminal opcodes with no label argument: control leaves the straight
# line here, so no sequence may continue past them. Ops that carry a
# label argument (jumps, checks, switches, iteration) are detected from
# opcode metadata instead of being listed.
TERMINAL_OPS = frozenset((
    'RETURN', 'RAISE', 'BREAK', 'CONTINUE',
    'YIELD', 'YIELD_FROM', 'AWAIT',
    'TAIL_CALL', 'TAIL_CALL_NAMES', 'TAIL_CALL_STAR',
    'THEEND',
))

LOOP_PUSH_OPS = frozenset(('PUSH_LOOP', 'PUSH_FOREACH'))

LOOP_POP_OPS = frozenset(('POP_LOOP', 'POP_FOREACH'))


def op_short_name(op) -> str:
    """
    Strip the KOS_OP_ prefix from an OpCode member name
    """

    name = op.name
    return name[7:] if name.startswith('KOS_OP_') else name


def build_block_enders() -> frozenset:
    """
    Compute the set of opcode names that end a straight-line run: any
    opcode with a LABEL field in its argument schema, plus the terminal
    set. Computed from OpCode metadata so new branching opcodes are
    picked up automatically.
    """

    from koskari.lib import OpCode, OpStyle

    enders = set(TERMINAL_OPS)
    for op in OpCode:
        style = getattr(op, 'style', None)
        if style == OpStyle.KOSTYLE_LABEL:
            enders.add(op_short_name(op))
            continue
        extra = getattr(op, 'extra', None)
        if extra and OpStyle.KOSTYLE_LABEL in tuple(extra):
            enders.add(op_short_name(op))
    return frozenset(enders)


def unwrap_code(obj) -> Optional[Any]:
    """
    Return the Code behind a collectable object, or None. Accepts Code,
    Expression, Function, demand-wrapped Function, and BoundMethod.
    """

    from koskari.lib import BoundMethod, Code, Expression, Function, demand

    if isinstance(obj, demand):
        obj = getattr(obj, '__wrapped__', None)
    if isinstance(obj, BoundMethod):
        obj = obj.__func__
        if isinstance(obj, demand):
            obj = getattr(obj, '__wrapped__', None)
    if isinstance(obj, Function):
        return obj.code
    if isinstance(obj, Expression):
        return obj.code
    if isinstance(obj, Code):
        return obj
    return None


def closure_codes(fn) -> Iterator[Any]:
    """
    Yield Code objects captured in a Python function's closure cells.
    Macro handlers arrive as Python lambdas closing over the compiled
    Koskari body (MacroTransform.handler cell 'raw').
    """

    for cell in fn.__closure__ or ():
        try:
            val = cell.cell_contents
        except ValueError:
            continue
        code = unwrap_code(val)
        if code is not None:
            yield code


def syntax_codes(label: str, syn) -> Iterator[Tuple[str, Any]]:
    """
    Walk a Syntax object's transformers for code-bearing attributes.
    Macro-based syntax carries its body as a Function behind the
    transform's handler; syntax-rules templates and Python specials
    contribute nothing.
    """

    import types

    for i, transform in enumerate(syn.transforms):
        members = getattr(transform, '__dict__', None)
        if not members:
            continue
        for key, val in members.items():
            code = unwrap_code(val)
            if code is not None:
                yield (f'{label}[{i}].{key}', code)
            elif isinstance(val, types.FunctionType):
                for code in closure_codes(val):
                    yield (f'{label}[{i}].{key}', code)


def collect_codes(modules: List[Any]) -> List[Tuple[str, Any]]:
    """
    Walk module namespaces for code-bearing objects, then recurse into
    each Code's constant pool for nested Code objects (closures, class
    bodies). Syntax objects are walked through their transformers to
    catch macro bodies. Returns (label, Code) pairs, deduplicated by
    identity.
    """

    from koskari.lib import Code
    from koskari.syntax import Syntax

    seen = set()
    found = []
    stack = []

    for mod in modules:
        for key, val in sorted(vars(mod).items()):
            if key.startswith('__'):
                continue
            label = f'{mod.__name__}.{key}'
            if isinstance(val, Syntax):
                stack.extend(syntax_codes(label, val))
                continue
            code = unwrap_code(val)
            if code is not None:
                stack.append((label, code))

    while stack:
        label, code = stack.pop(0)
        if id(code) in seen:
            continue
        seen.add(id(code))
        found.append((label, code))

        for i, val in enumerate(code.constants_pool):
            nested = unwrap_code(val)
            if isinstance(nested, Code) and id(nested) not in seen:
                name = nested.name or f'c#{i}'
                stack.append((f'{label}/{name}', nested))

    return found


def iter_blocks(code) -> Iterator[List[Tuple[str, int]]]:
    """
    Soften code and yield straight-line runs as lists of (op_name,
    loop_depth) pairs. A run starts fresh at every LABEL and stops
    after any block-ending opcode; POSITION and NOOP are transparent.
    Loop depth follows PUSH_LOOP/PUSH_FOREACH nesting in textual order.
    """

    from koskari.lib import OpCode

    enders = iter_blocks._enders
    depth = 0
    block = []

    for op_int, _arg in code.soften():
        name = op_short_name(OpCode(op_int))

        if name == 'LABEL':
            if block:
                yield block
            block = []
            continue

        if name in ('POSITION', 'NOOP'):
            continue

        if name in LOOP_PUSH_OPS:
            depth += 1
        elif name in LOOP_POP_OPS and depth > 0:
            depth -= 1

        block.append((name, depth))

        if name in enders:
            yield block
            block = []

    if block:
        yield block


class GramTally:
    """
    Accumulates raw and depth-weighted n-gram counts, exemplar
    locations, and the bigram successor table.
    """

    def __init__(self, sizes: Tuple[int, ...], weight_base: int):
        self.sizes = sizes
        self.weight_base = weight_base
        self.raw = {n: Counter() for n in sizes}
        self.weighted = {n: Counter() for n in sizes}
        self.exemplar: Dict[Tuple, str] = {}
        self.successors = defaultdict(Counter)
        self.op_counts = Counter()
        self.code_count = 0
        self.block_count = 0

    def feed(self, label: str, block: List[Tuple[str, int]]) -> None:
        self.block_count += 1
        names = [name for name, _depth in block]

        for name in names:
            self.op_counts[name] += 1
        for prev, succ in zip(names, names[1:]):
            self.successors[prev][succ] += 1

        for n in self.sizes:
            for i in range(len(block) - n + 1):
                gram = tuple(names[i:i + n])
                weight = self.weight_base ** block[i][1]
                self.raw[n][gram] += 1
                self.weighted[n][gram] += weight
                if gram not in self.exemplar:
                    self.exemplar[gram] = label


def analyze(
        codes: List[Tuple[str, Any]],
        sizes: Tuple[int, ...],
        weight_base: int) -> GramTally:
    """
    Run the n-gram tally over every collected code object
    """

    tally = GramTally(sizes, weight_base)
    for label, code in codes:
        tally.code_count += 1
        for block in iter_blocks(code):
            tally.feed(label, block)
    return tally


def print_gram_tables(tally: GramTally, top: int, file) -> None:
    for n in tally.sizes:
        weighted = tally.weighted[n]
        file.write(f'\n=== top {n}-grams'
                   f' (weighted, base {tally.weight_base}) ===\n')
        for gram, wcount in weighted.most_common(top):
            raw = tally.raw[n][gram]
            seq = ' '.join(gram)
            file.write(f'  {wcount:>12}  {raw:>6}  {seq}\n')
            file.write(f'  {"":>12}  {"":>6}    e.g. {tally.exemplar[gram]}\n')


def print_markov(tally: GramTally, threshold: int, file) -> None:
    file.write(f'\n=== successor distribution'
               f' (ops emitted >= {threshold} times) ===\n')
    for op, count in tally.op_counts.most_common():
        if count < threshold:
            break
        succs = tally.successors[op]
        total = sum(succs.values())
        if not total:
            continue
        parts = []
        for succ, scount in succs.most_common(4):
            pct = 100.0 * scount / total
            parts.append(f'{succ} {pct:.0f}%')
        file.write(f'  {op:<24} x{count:<7} -> {", ".join(parts)}\n')


def tally_to_json(tally: GramTally, top: int) -> Dict[str, Any]:
    out = {
        'codes': tally.code_count,
        'blocks': tally.block_count,
        'weight_base': tally.weight_base,
        'grams': {},
        'successors': {},
    }
    for n in tally.sizes:
        out['grams'][str(n)] = [
            {
                'seq': list(gram),
                'weighted': wcount,
                'raw': tally.raw[n][gram],
                'exemplar': tally.exemplar[gram],
            }
            for gram, wcount in tally.weighted[n].most_common(top)
        ]
    for op, succs in tally.successors.items():
        out['successors'][op] = dict(succs.most_common())
    return out


# Groups whose modules live outside the installed package join
# sys.path from these repository-relative directories.
GROUP_PATHS = {
    'churn': join(TOPDIR, 'examples', 'churn'),
    'life': join(TOPDIR, 'examples', 'life'),
}


def load_group(group: str) -> List[Any]:
    """
    Import and return the modules for a named group. Groups that live
    outside the installed package get their directory on sys.path first.
    """

    extra_path = GROUP_PATHS.get(group)
    if extra_path and extra_path not in sys.path:
        sys.path.insert(0, extra_path)

    import koskari  # noqa: F401  (activates the .ki import hook)

    return [importlib.import_module(name) for name in MODULE_GROUPS[group]]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--group', choices=(*MODULE_GROUPS, 'all'), default='all',
        help='module group to analyze (default: all groups)')
    parser.add_argument(
        '--top', type=int, default=25,
        help='rows per n-gram table (default: 25)')
    parser.add_argument(
        '--weight-base', type=int, default=100,
        help='per-loop-depth weight multiplier (default: 100)')
    parser.add_argument(
        '--markov-threshold', type=int, default=20,
        help='min emission count for the successor table (default: 20)')
    parser.add_argument(
        '--json', action='store_true',
        help='emit the report as JSON instead of text')
    parser.add_argument(
        '--dis', action='store_true',
        help='print full disassembly of each collected code object')
    options = parser.parse_args(argv)

    groups = list(MODULE_GROUPS) if options.group == 'all' \
        else [options.group]

    modules = []
    for group in groups:
        modules.extend(load_group(group))

    iter_blocks._enders = build_block_enders()

    codes = collect_codes(modules)

    if options.dis:
        import asyncio

        from koskari.dis import dis

        async def dump():
            for label, code in codes:
                sys.stdout.write(f'--- {label} ---\n')
                result = dis(code)
                if hasattr(result, '__await__'):
                    await result

        asyncio.run(dump())
        return 0

    tally = analyze(
        codes, sizes=(2, 3, 4), weight_base=options.weight_base)

    if options.json:
        json.dump(tally_to_json(tally, options.top), sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 0

    sys.stdout.write(f'codes: {tally.code_count}'
                     f'   blocks: {tally.block_count}'
                     f'   groups: {", ".join(groups)}\n')
    print_gram_tables(tally, options.top, sys.stdout)
    print_markov(tally, options.markov_threshold, sys.stdout)
    return 0


if __name__ == '__main__':
    sys.exit(main())


# The end.
