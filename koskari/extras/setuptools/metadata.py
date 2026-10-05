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
koskari.extras.setuptools.metadata

Pure-Python koskari_scripts metadata helpers shared by the build plugin and
koskari.launcher without importing the C extension or setuptools.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import re
import warnings


__all__ = (
    'EntryTargetError',
    'KOSKARI_PACKAGE_DATA_PATTERNS',
    'KOSKARI_SCRIPTS_GROUP',
    'KoskariScriptsSetupError',
    'augment_koskari_packages',
    'declared_packages',
    'entry_points_group_lines',
    'entry_points_lines_as_dict',
    'packages_for_module_name',
    'packages_from_koskari_scripts',
    'parse_entry_point_line',
    'parse_entry_target',
    'validate_entry_point_value',
    'validate_koskari_scripts',
    'warn_koskari_scripts_metadata',
    'warn_undeclared_script_packages',
)


KOSKARI_SCRIPTS_GROUP = 'koskari_scripts'

KOSKARI_PACKAGE_DATA_PATTERNS = ('*.ki', '*.kio')


class EntryTargetError(Exception):
    """
    Entry point value is not a supported module:callable target
    """


class KoskariScriptsSetupError(Exception):
    """
    Invalid or conflicting koskari_scripts packaging metadata
    """


def parse_entry_target(value):
    """
    Parse a koskari_scripts entry value into module and callable names

    v1 supports module:callable only. Bare script-module targets are rejected.

    :param value: entry point value string from packaging metadata
    :return: (module_name, callable_name) pair
    :raises EntryTargetError: when value is not module:callable form
    """

    value = value.strip()
    if not value:
        raise EntryTargetError("empty koskari_scripts entry value")

    if ':' not in value:
        raise EntryTargetError(
            f"koskari_scripts entry {value!r} must use"
            " module:callable form")

    module_name, callable_name = value.split(':', 1)
    module_name = module_name.strip()
    callable_name = callable_name.strip()

    if not module_name or not callable_name:
        raise EntryTargetError(
            f"koskari_scripts entry {value!r} must use"
            " module:callable form")

    if '.' in callable_name:
        raise EntryTargetError(
            f"nested callable paths are not supported in v1: {value!r}")

    return module_name, callable_name


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
    except EntryTargetError as exc:
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

    install_reqs = getattr(dist, 'install_requires', None) or []
    names = {requirement_name(req) for req in install_reqs}
    if 'koskari' not in names:
        warnings.warn(
            "koskari_scripts declared but koskari is not in install_requires",
            stacklevel=2)


# the leading project name of a PEP 508 requirement string
_REQUIREMENT_NAME = re.compile(r'\s*([A-Za-z0-9][A-Za-z0-9._-]*)')


def requirement_name(requirement):
    """
    The normalized project name a requirement string names, or None

    Specifiers, extras and markers after the name are ignored, and the
    name is compared the way package names are: lowercase, with runs of
    dots, dashes and underscores folded to one dash

    :param requirement: a requirement string such as koskari>=0.1
    """

    found = _REQUIREMENT_NAME.match(str(requirement))
    if not found:
        return None
    return re.sub(r'[-_.]+', '-', found.group(1)).lower()


def declared_packages(dist):
    """
    Return the package names explicitly declared on the Distribution

    :param dist: setuptools Distribution being finalized
    :return: sorted package names from dist.packages
    """

    packages = getattr(dist, 'packages', None) or []
    return sorted(set(packages))


def packages_for_module_name(module_name):
    """
    Return parent package names required for a dotted module name

    :param module_name: dotted module path from an entry point value
    :return: sorted package names
    """

    parts = module_name.split('.')
    if len(parts) < 2:
        return []

    return ['.'.join(parts[:index]) for index in range(1, len(parts))]


def packages_from_koskari_scripts(entry_points):
    """
    Derive package names referenced by koskari_scripts entry points

    :param entry_points: Distribution.entry_points value
    :return: set of package names
    """

    packages = set()
    for line in entry_points_group_lines(entry_points, KOSKARI_SCRIPTS_GROUP):
        _name, value = parse_entry_point_line(line)
        module_name, _callable_name = parse_entry_target(value)
        packages.update(packages_for_module_name(module_name))
    return packages


def warn_undeclared_script_packages(dist):
    """
    Warn when koskari_scripts targets are outside declared packages

    :param dist: setuptools Distribution being finalized
    """

    declared = set(declared_packages(dist))
    if not declared:
        return

    required = packages_from_koskari_scripts(dist.entry_points)
    missing = sorted(required.difference(declared))
    if not missing:
        return

    warnings.warn(
        "koskari_scripts entry points reference packages not listed in"
        f" packages={sorted(declared)!r}: {', '.join(missing)}",
        stacklevel=2)


def _ensure_package_data(dist, packages):
    package_data = dict(getattr(dist, 'package_data', None) or {})
    for package_name in packages:
        patterns = list(package_data.get(package_name, []))
        for pattern in KOSKARI_PACKAGE_DATA_PATTERNS:
            if pattern not in patterns:
                patterns.append(pattern)
        package_data[package_name] = patterns
    dist.package_data = package_data


def augment_koskari_packages(dist):
    """
    Add koskari source patterns to author-declared packages

    When koskari_scripts is declared, ensure each package listed in
    dist.packages includes *.ki and *.kio in package_data and set
    zip_safe False. Authors must declare packages= explicitly; this
    hook does not infer package names from entry points or filesystem
    scans.

    :param dist: setuptools Distribution being finalized
    """

    koskari_lines = entry_points_group_lines(
        dist.entry_points, KOSKARI_SCRIPTS_GROUP)
    if not koskari_lines:
        return

    packages = declared_packages(dist)
    if not packages:
        warnings.warn(
            'koskari_scripts declared but packages= is empty; declare'
            ' koskari packages explicitly in setup metadata',
            stacklevel=2)
        return

    warn_undeclared_script_packages(dist)
    _ensure_package_data(dist, packages)

    if getattr(dist, 'zip_safe', False):
        warnings.warn(
            "koskari_scripts packaging sets zip_safe=False so .ki files"
            " stay discoverable; the declared zip_safe=True is overridden",
            stacklevel=2)
    dist.zip_safe = False


# The end.
