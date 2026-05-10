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
koskari.compile.helpers_ng

Compile helpers for matcher_ng FORMALS captures (GROUP dict payload).
Does not import koskari.compile.helpers

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from typing import List

from ..exceptions import SpecialError
from ..lib import OpCode, keyword, nil, pair, symbol
from ..matcher_ng import FORMALS_SOURCE_KEY
from ..types import is_pair, is_symbol

from .compiler import ByteCodeFragment


__all__ = (
    "discover_docstring",
    "gather_formals",
    "helper_begin",
)


def is_lazygensym(x):
    """
    Return True if x is a lazy gensym (compile-time placeholder).
    Koskari currently has no lazy gensyms; always False
    """

    return False


_SYM_BEGIN = symbol('begin')
_SYM_COMPILE_BEGIN = symbol('compile/begin')
_BEGIN_HEADS = (_SYM_BEGIN, _SYM_COMPILE_BEGIN)


def discover_docstring(body):
    """
    Return the leading docstring str from a body list, or None.

    Does not mutate body. Leading ``begin`` forms are peeled for discovery
    only; the body is still compiled in full elsewhere

    :param body: proper list of forms (pair or nil)
    :return: str or None
    """

    while body is not nil:
        head, tail = body
        if isinstance(head, str):
            return head

        if head is nil or not is_pair(head):
            return None

        ihead, itail = head
        if ihead in _BEGIN_HEADS:
            if itail is nil:
                body = tail
            else:
                body = itail
        else:
            return None

    return None


def helper_begin(
        fragment: ByteCodeFragment,
        body: pair,
        tc: bool = False) -> ByteCodeFragment:
    """
    Compile a body list into a fragment

    :param fragment: the fragment to compile into
    :param body: the body list to compile
    :param tc: whether to compile in tail call optimization mode
    :return: the compiled fragment
    """

    if body is nil:
        fragment.instruction(OpCode.KOS_OP_CONST, None)
        return fragment

    compiler = fragment.compiler

    while True:
        fragment.position_of(body)

        expr, body = body
        if body is nil:
            fragment.merge(compiler.full_compile(expr, tc))
            break
        else:
            fragment.merge(compiler.full_compile(expr, False))
            fragment.instruction(OpCode.KOS_OP_POP, 1)

    return fragment


def _merge_formal_annotations(annotations, name_sym, ann_bundle):
    """
    Merge ann_bundle into annotations dict keyed by str(name_sym)
    """

    if not ann_bundle:
        return
    key = str(name_sym)
    if key in annotations:
        annotations[key].update(ann_bundle)
    else:
        annotations[key] = dict(ann_bundle)


def _attrs_frames_to_ann(attr_frames: List[dict], source):
    """
    Fold formal attribute REPEAT frames into (ann dict, default or None)
    """

    ann = {}
    default = None
    for afr in attr_frames:
        kw_cell = afr.get('kw')
        if kw_cell is None:
            continue
        kw = kw_cell.datum
        val_cell = afr.get('attr_val')
        if val_cell is None:
            raise SpecialError(
                f"missing value for formal attribute {kw!r}",
                source)
        val = val_cell.datum

        if kw == keyword('value'):
            default = val
        elif kw in (keyword('type'), keyword('')):
            ann['type'] = val
        else:
            raise SpecialError(
                f"unknown formal attribute {kw!r}",
                source)
    return ann, default


def _gather_one_positional_frame(fr: dict, source):
    """
    Map one positional REPEAT frame to (name, ann_bundle, default_expr)
    """

    name = None if (v := fr.get('name')) is None else v.datum
    typ = None if (v := fr.get('type')) is None else v.datum

    default_cell = fr.get('default')
    if default_cell is None:
        default_expr = None
    else:
        default_expr = default_cell.datum

    annotations = {}
    if typ is not None:
        annotations['type'] = typ

    attrs_cell = fr.get('attrs')
    if attrs_cell:
        ann2, default2 = _attrs_frames_to_ann(attrs_cell, source)
        for k2, v2 in ann2.items():
            if k2 in annotations and annotations[k2] != v2:
                raise SpecialError(
                    f"conflicting formal annotations for {name!r}",
                    source)
            annotations[k2] = v2
        if default2 is not None:
            if default_expr is not None:
                raise SpecialError(
                    f"duplicate default for formal {name!r}",
                    source)
            default_expr = default2

    if name is None:
        raise SpecialError(
            f"positional frame missing name: {fr!r}",
            source)
    if not (is_symbol(name) or is_lazygensym(name)):
        raise SpecialError(
            "positional formals must be symbols or bracket forms, not"
            f" {name!r}",
            source)

    return name, annotations, default_expr


def gather_formals(formals_group, source=None):
    """
    Parse matcher_ng FORMALS GROUP dict into six values matching
    koskari.compile.helpers.gather_formals

    :param formals_group: dict from captures[formals_slot] after match
    :param source: optional spine for error context; defaults to
        formals_group[FORMALS_SOURCE_KEY].datum when present
    :return: (positional, defaults, kwonly, star, starstar, annotations)
    :raises SpecialError: on invalid structure
    """


    if source is None:
        sk = formals_group.get(FORMALS_SOURCE_KEY)
        source = sk.datum if sk else None

    annotations = {}
    positional = []
    defaults = []

    for fr in formals_group.get('positionals', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid positional frame {fr!r}",
                source)
        name, ann_bundle, default = _gather_one_positional_frame(fr, source)
        positional.append(name)
        _merge_formal_annotations(annotations, name, ann_bundle)
        if default is not None:
            defaults.append((keyword(str(name)), default))

    for fr in formals_group.get('defaults', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid defaults frame {fr!r}",
                source)
        kw = None if (v := fr.get('kw')) is None else v.datum
        dflt = None if (v := fr.get('default')) is None else v.datum
        if kw is None:
            raise SpecialError(
                f"defaults frame missing keyword: {fr!r}",
                source)
        defaults.append((kw, dflt))

    kwonly = []
    for fr in formals_group.get('kwonly', ()):
        if not isinstance(fr, dict):
            raise SpecialError(
                f"invalid kwonly frame {fr!r}",
                source)
        name = None if (v := fr.get('name')) is None else v.datum
        if name is not None and (is_symbol(name) or is_lazygensym(name)):
            kwonly.append((keyword(str(name)), nil))
            continue
        kw = None if (v := fr.get('kw')) is None else v.datum
        dflt = None if (v := fr.get('default')) is None else v.datum
        if kw is not None:
            kwonly.append((kw, dflt))
        else:
            raise SpecialError(
                f"invalid kw-only frame {fr!r}",
                source)

    star_cell = formals_group.get('star')
    star = star_cell.datum if star_cell else None

    ss_cell = formals_group.get('starstar')
    starstar = ss_cell.datum if ss_cell else None
    if starstar and not is_symbol(starstar):
        raise SpecialError(
            f"** keyword requires symbol binding, not {starstar!r}",
            source)

    if star is not None and star is not nil:
        if not is_symbol(star):
            raise SpecialError(
                f"* keyword requires symbol binding, not {star!r}",
                source)

    return (positional, defaults, kwonly, star, starstar, annotations)


# The end.
