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
    build_proper, get_position, is_nil, is_pair,
    is_symbol, strip_mark, unpack)


__all__ = (
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
        return gensym(strip_mark(atom), marks=atom.marks + (mark,))
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
    the template itself introduces.

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
    cell reached twice is a cycle, which source cannot hold

    :param form: the handler's output
    :param mark: the expansion mark
    :param seen: ids of the cells already on the walk
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


# The end.
