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
koskari.extras.setuptools.validate

Validation helpers for koskari_scripts distribution metadata.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import warnings

from setuptools.errors import SetupError

from ...launcher import KOSKARI_SCRIPTS_GROUP, parse_entry_target


__all__ = (
    'KoskariScriptsSetupError',
    'entry_points_group_lines',
    'entry_points_lines_as_dict',
    'parse_entry_point_line',
    'validate_entry_point_value',
    'validate_koskari_scripts',
    'warn_koskari_scripts_metadata',
)


class KoskariScriptsSetupError(SetupError):
    """
    Invalid or conflicting koskari_scripts packaging metadata
    """


def entry_points_lines_as_dict(entry_points):
    """
    Normalize dist.entry_points into a group-to-lines mapping

    :param entry_points: Distribution.entry_points value
    :return: dict mapping group name to list of 'name = value' strings
    """

    if not entry_points:
        return {}

    if isinstance(entry_points, dict):
        result = {}
        for group, lines in entry_points.items():
            if lines is None:
                result[group] = []
            elif isinstance(lines, str):
                result[group] = [lines]
            else:
                result[group] = list(lines)
        return result

    if hasattr(entry_points, 'select'):
        result = {}
        groups = getattr(entry_points, 'groups', None)
        if groups is None and hasattr(entry_points, 'names'):
            groups = entry_points.names()
        if groups is None:
            return {}
        for group in groups:
            result[group] = [
                f'{ep.name} = {ep.value}'
                for ep in entry_points.select(group=group)
            ]
        return result

    return {}


def entry_points_group_lines(entry_points, group):
    """
    Return normalized entry point lines for one metadata group

    :param entry_points: Distribution.entry_points value
    :param group: entry point group name
    :return: list of 'name = value' strings
    """

    mapping = entry_points_lines_as_dict(entry_points)
    return list(mapping.get(group, []))


def parse_entry_point_line(line):
    """
    Split an entry point declaration into name and value

    :param line: 'name = module:object' string
    :return: (name, value) pair
    :raises KoskariScriptsSetupError: when the line is malformed
    """

    line = line.strip()
    if not line or '=' not in line:
        raise KoskariScriptsSetupError(
            f'invalid entry point line {line!r}')

    name, value = line.split('=', 1)
    name = name.strip()
    value = value.strip()
    if not name or not value:
        raise KoskariScriptsSetupError(
            f"invalid entry point line {line!r}")

    return name, value


def validate_entry_point_value(value):
    """
    Validate a koskari_scripts entry value uses module:callable form

    :param value: entry point value string
    :raises KoskariScriptsSetupError: when the value is unsupported
    """

    try:
        parse_entry_target(value)
    except Exception as exc:
        raise KoskariScriptsSetupError(
            "invalid koskari_scripts entry value"
            f" {value!r}: {exc}") from exc


def validate_koskari_scripts(dist):
    """
    Enforce hard validation rules for koskari_scripts metadata

    :param dist: setuptools Distribution being finalized
    :raises KoskariScriptsSetupError: on duplicate names or collisions
    """

    koskari_lines = entry_points_group_lines(
        dist.entry_points, KOSKARI_SCRIPTS_GROUP)
    if not koskari_lines:
        return

    console_lines = entry_points_group_lines(
        dist.entry_points, 'console_scripts')

    koskari_names = []
    for line in koskari_lines:
        name, value = parse_entry_point_line(line)
        koskari_names.append(name)
        validate_entry_point_value(value)

    if len(koskari_names) != len(set(koskari_names)):
        raise KoskariScriptsSetupError(
            f"duplicate names in koskari_scripts: {koskari_names!r}")

    console_names = set()
    for line in console_lines:
        name, _value = parse_entry_point_line(line)
        console_names.add(name)

    overlap = sorted(set(koskari_names).intersection(console_names))
    if overlap:
        raise KoskariScriptsSetupError(
            "koskari_scripts names must not also appear in console_scripts:"
            f" {', '.join(overlap)}")


def warn_koskari_scripts_metadata(dist):
    """
    Emit soft warnings for common koskari_scripts packaging mistakes

    :param dist: setuptools Distribution being finalized
    """

    koskari_lines = entry_points_group_lines(
        dist.entry_points, KOSKARI_SCRIPTS_GROUP)
    if not koskari_lines:
        return

    install_reqs = set(getattr(dist, 'install_requires', None) or [])
    if 'koskari' not in install_reqs:
        warnings.warn(
            "koskari_scripts declared but koskari is not in install_requires",
            stacklevel=2)

    if getattr(dist, 'zip_safe', False):
        warnings.warn(
            "koskari_scripts with zip_safe=True may prevent .ki"
            " discovery; setuptools plugin sets zip_safe=False when"
            " koskari packages are discovered",
            stacklevel=2)


# The end.
