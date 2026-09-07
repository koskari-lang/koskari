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
koskari.fences.helpers

Fence handler wrapper, decorator, resolution, and META plumbing for the
compile/apply-fence special.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ..types import is_keyword, is_pair


class FenceHandler:
    """
    Wrapper for fence handlers.

    A handler is called as read_fn(raw, meta) with the raw fenced source
    text and a metadata dict, and returns Koskari source to compile in
    place of the fence.
    """

    def __init__(self, name, read_fn):
        self.name = name
        self.read = read_fn


def find_fence(ns, name):
    """
    Resolve fence/<name> in the module namespace

    A bare-name binding resolves only when it is a ``FenceHandler``; a
    ``fence/``-prefixed binding resolves when it is a ``FenceHandler``
    or any callable, so modules may define their own fences as plain
    functions bound to fence/<name>.

    :param ns: mapping used for fence handler lookup
    :param name: symbol or string fence format name (e.g. json, bash)
    :return: FenceHandler instance or callable, or None
    """

    name = str(name)

    # fence handlers live in the builtins below a module namespace's
    # own values; a plain dict has no resolve and answers as-is
    lookup = getattr(ns, 'resolve', None) or ns.get

    if not name.startswith("fence/"):
        found = lookup(name, None)
        if isinstance(found, FenceHandler):
            return found
        name = "fence/" + name

    found = lookup(name, None)
    if isinstance(found, FenceHandler) or callable(found):
        return found
    return None


def _plist_items(form, what):
    """
    Collect (str-key, value) pairs from a keyword/value plist cons

    :param form: a proper cons list of alternating keywords and values
    :param what: description of the plist for error messages
    :return: list of (str, value) tuples
    :raises ValueError: when the plist is malformed
    """

    items = []
    while form:
        if not is_pair(form):
            raise ValueError(f"{what} must be a proper keyword/value plist")
        key, form = form
        if not is_keyword(key):
            raise ValueError(f"{what} key {key} is not a keyword")
        if not (form and is_pair(form)):
            raise ValueError(f"{what} is missing a value for {key}")
        value, form = form
        items.append((str(key), value))
    return items


def meta_plist_to_dict(meta, name=None, filename=None):
    """
    Convert a fence META keyword plist into a plain metadata dict

    The result always carries ``name``, ``filename``, ``line``, ``col``,
    and ``options`` keys; ``options`` is a dict of the opener-line
    keyword/value pairs. The ``filename`` argument is a fallback used
    when META does not carry one.

    :param meta: META keyword plist cons, or nil/None
    :param name: the fence format name
    :param filename: fallback filename when META has none
    :return: metadata dict
    :raises ValueError: when the plist is malformed
    """

    result = {
        "name": str(name) if name is not None else None,
        "filename": filename,
        "line": None,
        "col": None,
        "options": {},
    }

    if meta is None:
        return result

    for key, value in _plist_items(meta, "fence META"):
        if key == "options":
            result["options"] = dict(_plist_items(value, "fence options"))
        else:
            result[key] = value

    return result


# The end.
