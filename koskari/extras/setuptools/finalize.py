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
koskari.extras.setuptools.finalize

Setuptools finalize hook that mirrors koskari_scripts into console_scripts.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...launcher import DISPATCH_TARGET, KOSKARI_SCRIPTS_GROUP
from .validate import (entry_points_group_lines, entry_points_lines_as_dict,
                       parse_entry_point_line, validate_koskari_scripts,
                       warn_koskari_scripts_metadata)


__all__ = (
    'mirror_koskari_scripts',
)


def _ensure_console_scripts_list(mapping):
    """
    Return the console_scripts line list, creating it when absent

    :param mapping: normalized entry_points dict
    :return: mutable list of console_scripts lines
    """

    lines = mapping.setdefault('console_scripts', [])
    if isinstance(lines, str):
        lines = [lines]
        mapping['console_scripts'] = lines
    return lines


def mirror_koskari_scripts(dist):
    """
    setuptools.finalize_distribution_options hook

    Mirror each koskari_scripts entry into console_scripts pointing at
    koskari.launcher.dispatch so pip generates bin/ wrappers.

    :param dist: setuptools Distribution being finalized
    """

    koskari_lines = entry_points_group_lines(
        dist.entry_points, KOSKARI_SCRIPTS_GROUP)
    if not koskari_lines:
        return

    validate_koskari_scripts(dist)
    warn_koskari_scripts_metadata(dist)

    mapping = entry_points_lines_as_dict(dist.entry_points)
    console_lines = _ensure_console_scripts_list(mapping)

    existing = set()
    for line in console_lines:
        name, _value = parse_entry_point_line(line)
        existing.add(name)

    for line in koskari_lines:
        name, _value = parse_entry_point_line(line)
        mirror_line = '%s = %s' % (name, DISPATCH_TARGET)
        if mirror_line in console_lines:
            continue
        if name in existing:
            continue
        console_lines.append(mirror_line)
        existing.add(name)

    dist.entry_points = mapping


# The end.
