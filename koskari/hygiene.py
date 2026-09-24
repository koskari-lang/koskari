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
koskari.hygiene

Template expansion and expansion finalize for syntax transformers.

Hygiene is provenance: a symbol a template introduced becomes a gensym
carrying the expansion that introduced it, and the compiler declares
and resolves it under the spelling the gensym derives. A keyword is a
label and carries no marks. Material substituted from the use site
arrives in Datum boxes from the matcher and is never marked. Finalize
peels the Datum boxes for compilation and leaves the gensyms for the
compiler

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.exceptions import CompilerError
from koskari.lib import (
    Datum, bracket_cons, bracket_nil, bracket_pair, cons, gensym)
from koskari.types import (
    build_proper, get_position, is_nil, is_pair, is_symbol, unpack)


__all__ = (
    "Resolver",
    "candidates",
    "declared_spellings",
    "expand_template_with_hygiene",
    "finalize_expansion",
    "mark_atom",
    "mark_expansion",
    "peel_datums",
)


# --- Marking


def mark_atom(atom, mark):
    """
    The atom scoped by ``mark``: a symbol becomes a gensym over it, a
    gensym gains ``mark`` as its newest, and anything else, keywords
    included, is returned unchanged

    :param atom: a source atom
    :param mark: the expansion mark, an int
    """

    if isinstance(atom, gensym):
        return gensym(atom.base, marks=atom.marks + (mark,))
    if is_symbol(atom):
        return gensym(atom, marks=mark)
    return atom


# --- Template expansion


def _collect_symbols(expr, out):
    if is_symbol(expr):
        out.add(expr)
        return

    if is_pair(expr):
        for elem in unpack(expr):
            if is_symbol(elem):
                out.add(elem)
            elif is_pair(elem):
                _collect_symbols(elem, out)


def _binding_value_for_expand(val):
    """
    Peel a single outer ``Datum`` wrapper when present so list, pair, or tuple
    shape is visible to template expansion. The bindings dict still holds
    matcher captures unchanged elsewhere
    """

    return val.datum if isinstance(val, Datum) else val


def _repeat_seq(val):
    """
    Return the ellipsis-dimension sequence a binding still carries, or None.

    Repeat captures are promoted to bare Python ``list``/``tuple`` columns (one
    entry per iteration of the enclosing ``...``); a rank-N variable nests N
    such sequences deep, with leaves that are ``Datum`` wrappers or plain
    captured atoms. A Koskari pair, a ``Datum``, or an atom is a *leaf*, not a
    dimension, so only a bare ``list``/``tuple`` (after peeling one ``Datum``)
    counts as an unconsumed ellipsis level
    """

    peeled = _binding_value_for_expand(val)
    if isinstance(peeled, (list, tuple)):
        return peeled
    return None


def _controlling_seqs(template, bindings):
    """
    Map each symbol in ``template`` that still carries an ellipsis dimension to
    its current-level sequence. These variables drive one ``...`` iteration and
    are peeled one dimension per level via :func:`_slice_bindings`
    """

    syms = set()
    _collect_symbols(template, syms)
    out = {}
    for s in syms:
        if s in bindings:
            seq = _repeat_seq(bindings[s])
            if seq is not None:
                out[s] = seq
    return out


def _ellipsis_count(controlling, template):
    """
    Common iteration length across all controlling sequences at one ``...``
    level, or 0 when none are present. Divergent lengths are an error
    """

    counts = {len(seq) for seq in controlling.values()}
    if not counts:
        return 0
    if len(counts) != 1:
        raise CompilerError(
            'incompatible ellipsis repetition lengths', template)
    return next(iter(counts))


def _slice_bindings(bindings, controlling, index):
    """
    Narrow ``bindings`` for one iteration of a ``...`` level: each controlling
    variable is replaced by its ``index``-th element (peeling one ellipsis
    dimension), while every other binding passes through unchanged
    """

    sub = dict(bindings)
    for sym, seq in controlling.items():
        sub[sym] = seq[index]
    return sub


def expand_template_with_hygiene(template, bindings, mark=None):
    """
    Substitute pattern bindings into a template and mark the symbols
    the template itself introduces

    Ellipsis repetition is depth-aware: a variable captured under N pattern
    ellipses carries N nested repeat sequences, and each template ``...`` peels
    exactly one dimension (see :func:`_slice_bindings`). A variable referenced
    under fewer ellipses than its rank still carries an unconsumed sequence at
    use site and raises, rather than splicing a raw column into the output

    :param template: template form (pairs, symbols, or opaque atoms)
    :param bindings: dict from pattern variable symbol to capture value
        (often ``Datum``-wrapped); a single outer ``Datum`` wrapper is peeled
        only when expanding list or ellipsis structure, not when returning
        an unexpanded binding reference unchanged
    :param mark: the expansion mark to box template atoms with, or None
        to substitute without marking
    """

    if is_symbol(template):
        if template in bindings:
            val = bindings[template]
            if _repeat_seq(val) is not None:
                raise CompilerError(
                    'ellipsis variable used at insufficient ellipsis depth',
                    template)
            return val

        if mark is None:
            return template
        return mark_atom(template, mark)

    if not is_pair(template):
        return template

    items = list(unpack(template))
    out = []
    i = 0
    while i < len(items):
        elem = items[i]

        rep = (i + 1 < len(items) and items[i + 1] is Ellipsis)
        if not rep:
            out.append(
                expand_template_with_hygiene(elem, bindings, mark))
            i += 1
            continue

        # a lone pattern variable bound to a non-repeat value (a pair spine
        # from a REST capture) splices like quasiquote ,@ rather than iterating
        if (is_symbol(elem)
                and elem in bindings
                and _repeat_seq(bindings[elem]) is None):
            spliced = _binding_value_for_expand(bindings[elem])
            if spliced is not None:
                out.extend(unpack(spliced))
            i += 2
            continue

        controlling = _controlling_seqs(elem, bindings)
        count = _ellipsis_count(controlling, elem)
        for rep_i in range(count):
            sub = _slice_bindings(bindings, controlling, rep_i)
            out.append(
                expand_template_with_hygiene(elem, sub, mark))
        i += 2

    # preserve the template's delimiter style: a bracket-pair template
    # rebuilds as a bracket list (terminated by bracket-nil), so a [ ] in a
    # syntax-rules template survives expansion instead of flattening to ( )
    if isinstance(template, bracket_pair):
        return bracket_cons(*out, bracket_nil) if out else bracket_nil

    return build_proper(*out)


# --- Expansion finalize


def mark_expansion(form, mark, seen=None):
    """
    A copy of a procedural expansion with every symbol outside a
    ``Datum`` scoped by ``mark``. A ``Datum`` is substituted material
    and is carried over as it is; a pair inside one is never entered.
    Pair cells are rebuilt with their positions and delimiter style. A
    cell that encloses itself is a cycle, which source cannot hold; a
    sub-form placed at more than one position is copied at each

    :param form: the handler's output
    :param mark: the expansion mark
    :param seen: ids of the cells enclosing this form on the walk
    :return: the marked copy
    :raises CompilerError: when the output is cyclic
    """

    if isinstance(form, Datum):
        return form

    if not is_pair(form) or is_nil(form):
        return mark_atom(form, mark)

    if seen is None:
        seen = set()

    items = []
    tail = form
    while is_pair(tail) and not is_nil(tail):
        if id(tail) in seen:
            raise CompilerError(
                "macro expansion is cyclic; a form cannot contain itself",
                form)
        seen.add(id(tail))
        items.append(tail)
        tail = tail.cdr
        if isinstance(tail, Datum):
            break

    if isinstance(tail, Datum):
        end = tail
    else:
        end = mark_atom(tail, mark)

    out = end
    for cell in reversed(items):
        car = mark_expansion(cell.car, mark, seen)
        if isinstance(cell, bracket_pair):
            out = bracket_cons(car, out)
        else:
            out = cons(car, out)
        pos = get_position(cell)
        if pos:
            out.set_position(pos)

    # this spine is left; its cells may appear again under a sibling
    seen.difference_update(id(cell) for cell in items)
    return out


def peel_datums(form):
    """
    Peel ``Datum`` boxes off a finished expansion in place, so the
    compiler sees the substituted source. A ``Datum`` car is unboxed and
    its position moved onto the cell; the material inside is the user's
    code and is not entered. ``Datum`` layers along a cdr chain are
    unboxed the same way

    :param form: expansion output (pairs, atoms, ``Datum``, gensyms)
    :rtype: same root object as ``form`` after in-place updates where
        applicable
    """

    seen = set()

    def peel_car_in_place(nx):
        c = nx.car
        first_pos = None
        if isinstance(c, Datum):
            first_pos = c.position
            c = c.datum
        if first_pos is not None:
            nx.position = first_pos
        nx.car = c
        return nx.car

    def peel_cdr_datums_in_place(nx):
        tail = nx.cdr
        while isinstance(tail, Datum):
            tail = tail.datum
            nx.cdr = tail
            tail = nx.cdr
        return tail

    def walk_spine(nx):
        while not is_nil(nx):
            if not is_pair(nx):
                return
            i = id(nx)
            if i in seen:
                nx = nx.cdr
                continue
            seen.add(i)

            car_was_datum = isinstance(nx.car, Datum)
            c = peel_car_in_place(nx)
            if is_pair(c) and not car_was_datum:
                walk_spine(c)

            tail = peel_cdr_datums_in_place(nx)
            if not is_pair(tail):
                return
            nx = tail

    if isinstance(form, Datum):
        form = form.datum
    if is_pair(form):
        walk_spine(form)
    return form


def finalize_expansion(form, mark=None):
    """
    Finish one expansion for the compiler: mark a procedural arm's
    output when ``mark`` is given, then peel the ``Datum`` boxes

    :param form: the expansion output
    :param mark: the expansion mark for a procedural arm, or None when
        transcription marked the output already
    :return: the finished form
    """

    if mark is not None:
        form = mark_expansion(form, mark)
    return peel_datums(form)


# --- Resolution

def candidates(atom):
    """
    The symbols a reference may resolve to, longest first: a gensym,
    then the gensym under its marks with the newest removed, down to
    its base. A bare symbol has itself alone

    :param atom: a symbol or gensym
    :rtype: list of symbol
    """

    base = atom.base
    if base is atom:
        return [atom]
    marks = atom.marks
    out = [atom]
    for n in range(len(marks) - 1, 0, -1):
        out.append(gensym(base, marks=marks[:n]))
    out.append(base)
    return out


def declared_spellings(fragment):
    """
    The spellings a fragment declares or pulls from the module
    """

    return {str(n) for n in fragment.declares} | {
        str(n) for n in fragment._request_global}


class Resolver:
    """
    The declarations visible to one completing fragment: its own, those
    of the fragments enclosing it, and the module's bound names

    A marked spelling bound in the module was defined by an earlier
    turn, as a macro-generating macro's expansion leaves behind

    :param fragment: the fragment being completed
    :param ancestors: the fragments enclosing it, outermost first
    :param module_names: the module's bound names
    """

    __slots__ = ("declared", "module_names")


    def __init__(self, fragment, ancestors, module_names):
        self.declared = [declared_spellings(fragment)]
        self.declared.extend(
            declared_spellings(f) for f in reversed(ancestors))
        self.module_names = module_names


    def choose(self, atom, scopes=()):
        """
        The first candidate for atom whose spelling is declared in an
        inline scope, the fragment, an ancestor, or the module; the
        base when none is

        :param atom: a symbol or gensym
        :param scopes: spelling sets of the inline scopes open around
            the reference, innermost first
        """

        cands = candidates(atom)
        for cand in cands[:-1]:
            spelling = str(cand)
            if any(spelling in s for s in scopes) \
                    or any(spelling in s for s in self.declared) \
                    or spelling in self.module_names:
                return cand
        return cands[-1]


    def resolve(self, fragment):
        """
        Resolve every marked name the fragment references to the binding
        it names, in place: its request buckets and the variable operands
        of its instruction stream

        A marked reference tries its key, then the key with the newest
        mark removed, down to the bare name, against the declarations
        visible from its position: the inline scopes open around it in
        the instruction stream, the fragment's own, and those of the
        fragments enclosing it, which are final by the time it completes

        :param fragment: the fragment being completed
        """

        for bucket in (fragment.requests, fragment.requests_for_write,
                       fragment._request_global):
            seen = set()
            out = []
            for sym in bucket:
                chosen = self.choose(sym)
                if chosen not in seen:
                    seen.add(chosen)
                    out.append(chosen)
            bucket[:] = out

        # an instruction operand is a spelling; the gensym it names was
        # noted when the operand was emitted
        marks = fragment.resolution.marks

        def choose_spelling(name, scopes):
            mark = marks.get(name)
            if mark is None:
                return name
            return str(self.choose(mark, scopes))

        fragment.rewrite_vars(choose_spelling)


# The end.
