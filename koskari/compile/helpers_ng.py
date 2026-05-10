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


from typing import Any, Dict, List

from ..exceptions import SpecialError
from ..lib import OpCode, keyword, nil, pair, symbol
from ..matcher_ng import ABSENT, FORMALS_SOURCE_KEY
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


def _formal_kw_tag_to_key(kw):
    """
    Map keyword atom to annotation key; empty keyword (``::`` sugar) is type
    """

    raw = str(kw)
    if raw.endswith(':'):
        raw = raw[:-1]
    if raw == '':
        return 'type'
    return raw


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


def _frame_cell_datum(fr: Dict[str, Any], key: str):
    """
    Read a Datum-backed frame slot; return unboxed value or None
    """

    v = fr.get(key)
    if v is None or v is ABSENT:
        return None
    return v.datum


def _attrs_frames_to_ann(attr_frames: List[dict], source):
    """
    Fold formal attribute REPEAT frames into (ann dict, default or None)
    """

    ann = {}
    default = None
    for afr in attr_frames:
        kw_cell = afr.get('kw')
        if kw_cell is None or kw_cell is ABSENT:
            continue
        kw = kw_cell.datum
        val_cell = afr.get('attr_val')
        if val_cell is None or val_cell is ABSENT:
            raise SpecialError(
                "missing value for formal attribute %r" % kw,
                source)
        val = val_cell.datum
        key = _formal_kw_tag_to_key(kw)
        if key == 'value':
            default = val
        elif key == 'type':
            ann['type'] = val
        else:
            raise SpecialError(
                "unknown formal attribute %r" % kw,
                source)
    return ann, default


def _gather_one_positional_frame(fr: dict, source):
    """
    Map one positional REPEAT frame to (name, ann_bundle, default_expr)
    """

    name = _frame_cell_datum(fr, 'name')
    typ = _frame_cell_datum(fr, 'type')
    default_cell = fr.get('default')
    if default_cell is None or default_cell is ABSENT:
        default_expr = None
    else:
        default_expr = default_cell.datum

    annotations = {}
    if typ is not None:
        annotations['type'] = typ

    attrs_cell = fr.get('attrs')
    if attrs_cell is not None and attrs_cell is not ABSENT:
        attrs_list = attrs_cell.datum
        if not isinstance(attrs_list, list):
            raise SpecialError(
                "positional attrs must be a list of frames, not %r"
                % (attrs_list,),
                source)
        ann2, default2 = _attrs_frames_to_ann(attrs_list, source)
        for k2, v2 in ann2.items():
            if k2 in annotations and annotations[k2] != v2:
                raise SpecialError(
                    "conflicting formal annotations for %r" % name,
                    source)
            annotations[k2] = v2
        if default2 is not None:
            if default_expr is not None:
                raise SpecialError(
                    "duplicate default for formal %r" % name,
                    source)
            default_expr = default2

    if name is None:
        raise SpecialError(
            "positional frame missing name: %r" % fr,
            source)
    if not (is_symbol(name) or is_lazygensym(name)):
        raise SpecialError(
            "positional formals must be symbols or bracket forms, not %r"
            % name,
            source)

    return name, annotations, default_expr


def _grp_frames(grp: dict, key: str) -> List[dict]:
    """
    List of REPEAT frame dicts for an optional FORMALS inner slot
    """

    v = grp.get(key)
    if v is None or v is ABSENT:
        return []
    d = v.datum
    if d is None:
        return []
    if not isinstance(d, list):
        return []
    return d


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

    if not isinstance(formals_group, dict):
        raise SpecialError(
            "formals_group must be a FORMALS GROUP dict, not %r"
            % (type(formals_group).__name__,),
            source)

    sk = formals_group.get(FORMALS_SOURCE_KEY)
    if source is None and sk is not None and sk is not ABSENT:
        source = sk.datum

    annotations = {}
    positional = []
    defaults = []

    for fr in _grp_frames(formals_group, 'positionals'):
        if not isinstance(fr, dict):
            raise SpecialError(
                "invalid positional frame %r" % fr,
                source)
        name, ann_bundle, default = _gather_one_positional_frame(fr, source)
        positional.append(name)
        _merge_formal_annotations(annotations, name, ann_bundle)
        if default is not None:
            defaults.append((keyword(str(name)), default))

    for fr in _grp_frames(formals_group, 'defaults'):
        if not isinstance(fr, dict):
            raise SpecialError(
                "invalid defaults frame %r" % fr,
                source)
        kw = _frame_cell_datum(fr, 'kw')
        dflt = _frame_cell_datum(fr, 'default')
        if kw is None:
            raise SpecialError(
                "defaults frame missing keyword: %r" % fr,
                source)
        defaults.append((kw, dflt))

    kwonly = []
    for fr in _grp_frames(formals_group, 'kwonly'):
        if not isinstance(fr, dict):
            raise SpecialError(
                "invalid kwonly frame %r" % fr,
                source)
        name = _frame_cell_datum(fr, 'name')
        if name is not None and (is_symbol(name) or is_lazygensym(name)):
            kwonly.append((keyword(str(name)), nil))
            continue
        kw = _frame_cell_datum(fr, 'kw')
        dflt = _frame_cell_datum(fr, 'default')
        if kw is not None:
            kwonly.append((kw, dflt))
        else:
            raise SpecialError(
                "invalid kw-only frame %r" % fr,
                source)

    star_cell = formals_group.get('star')
    if star_cell is None or star_cell is ABSENT:
        star = None
    else:
        star = star_cell.datum

    ss_cell = formals_group.get('starstar')
    if ss_cell is None or ss_cell is ABSENT:
        starstar = None
    else:
        starstar = ss_cell.datum
        if starstar is not None and not (
                is_symbol(starstar) or is_lazygensym(starstar)):
            raise SpecialError(
                "** keyword requires symbol binding, not %r" % starstar,
                source)

    if star is not None and star is not nil:
        if not (is_symbol(star) or is_lazygensym(star)):
            raise SpecialError(
                "* keyword requires symbol binding, not %r" % star,
                source)

    return (positional, defaults, kwonly, star, starstar, annotations)


# The end.
