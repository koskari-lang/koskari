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
koskari.extras.setuptools.packages

Augment author-declared packages with koskari source package_data.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import os
import warnings

from ...launcher import KOSKARI_SCRIPTS_GROUP, parse_entry_target
from .validate import (entry_points_group_lines, parse_entry_point_line)


__all__ = (
    'KOSKARI_PACKAGE_DATA_PATTERNS',
    'augment_koskari_packages',
    'declared_packages',
    'packages_for_module_name',
    'packages_from_koskari_scripts',
    'scan_koskari_packages',
    'warn_undeclared_script_packages',
)


KOSKARI_SOURCE_SUFFIXES = ('.ki', '.kio')
KOSKARI_PACKAGE_DATA_PATTERNS = ('*.ki', '*.kio')

EXCLUDE_DIR_NAMES = frozenset((
    '.git',
    '.tox',
    '__pycache__',
    'build',
    'dist',
    'node_modules',
))


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
        'koskari_scripts entry points reference packages not listed in'
        ' packages=%r: %s'
        % (sorted(declared), ', '.join(missing)),
        stacklevel=2)


def _should_skip_dir(name):
    if name in EXCLUDE_DIR_NAMES:
        return True
    if name.endswith('.egg-info'):
        return True
    return False


def _package_name_for_dir(root, dirpath):
    rel = os.path.relpath(dirpath, root)
    if rel == '.':
        return None
    return rel.replace(os.sep, '.')


def _packages_for_tree_root(root):
    packages = set()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            name for name in dirnames
            if not _should_skip_dir(name)
        ]
        has_koskari_source = any(
            filename.endswith(KOSKARI_SOURCE_SUFFIXES)
            for filename in filenames
        )
        if not has_koskari_source:
            continue

        package_name = _package_name_for_dir(root, dirpath)
        if not package_name:
            continue

        packages.add(package_name)
        parts = package_name.split('.')
        for index in range(1, len(parts)):
            packages.add('.'.join(parts[:index]))

    return packages


def _project_root(dist):
    script_name = getattr(dist, 'script_name', None)
    if script_name:
        return os.path.dirname(os.path.abspath(script_name))
    return os.getcwd()


def _scan_roots(dist, project_root):
    package_dir = getattr(dist, 'package_dir', None) or {}
    if not package_dir:
        return [project_root]

    roots = []
    seen = set()
    for path in package_dir.values():
        abspath = os.path.normpath(os.path.join(project_root, path))
        if abspath in seen:
            continue
        seen.add(abspath)
        roots.append(abspath)

    if not roots:
        roots.append(project_root)
    return roots


def scan_koskari_packages(dist):
    """
    Walk project source roots and find directories containing koskari sources

    Diagnostic helper only; package lists are not inferred from this scan
    during augment_koskari_packages.

    :param dist: setuptools Distribution being finalized
    :return: set of package names seen on disk
    """

    project_root = _project_root(dist)
    packages = set()
    for root in _scan_roots(dist, project_root):
        packages.update(_packages_for_tree_root(root))
    return packages


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
    dist.zip_safe = False


# The end.
