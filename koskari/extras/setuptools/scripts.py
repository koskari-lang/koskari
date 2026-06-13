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
koskari.extras.setuptools.scripts

Custom setuptools ScriptWriter for koskari_scripts installed commands.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import textwrap

from setuptools import _scripts as scripts
from setuptools._importlib import metadata as setuptools_metadata
from setuptools.warnings import SetuptoolsWarning

from ...launcher import KOSKARI_SCRIPTS_GROUP
from .validate import entry_points_group_lines, parse_entry_point_line


__all__ = (
    'KoskariScriptWriter',
    'KoskariWindowsExecutableLauncherWriter',
    'KoskariWindowsScriptWriter',
    'ensure_koskari_script_writer',
    'koskari_script_names',
)


_SCRIPT_WRITER_PATCHED = False


def koskari_script_source(spec, group, name):
    src = f"""
    # KOSKARI-SCRIPT-ENTRY: {spec!r},{group!r},{name!r}
    import re
    import sys

    from koskari.launcher import run_resolved_script

    if __name__ == '__main__':
        sys.argv[0] = re.sub(r'(-script\\.pyw?|\\.exe)?$', '', sys.argv[0])
        sys.exit(run_resolved_script({spec!r}, {name!r}))
    """
    return textwrap.dedent(src).lstrip()


def koskari_script_names(dist):
    """
    Return koskari_scripts entry names declared on a distribution

    :param dist: setuptools Distribution being built
    :return: set of script basenames
    """

    eps = dist.entry_points
    if hasattr(eps, 'select'):
        return {ep.name for ep in eps.select(group=KOSKARI_SCRIPTS_GROUP)}

    names = set()
    for line in entry_points_group_lines(eps, KOSKARI_SCRIPTS_GROUP):
        name, _value = parse_entry_point_line(line)
        names.add(name)
    return names


class KoskariScriptWriter(scripts.ScriptWriter):
    """
    Emit direct run_resolved_script stubs for koskari_scripts commands
    """

    @classmethod
    def get_args(cls, dist, header=None):
        if not hasattr(dist, 'entry_points'):
            SetuptoolsWarning.emit("Unsupported distribution encountered.")
            dist = setuptools_metadata.Distribution.at(dist.egg_info)

        names = koskari_script_names(dist)

        if header is None:
            header = cls.get_header()
        spec = f'{dist.name}=={dist.version}'

        for type_ in ('console', 'gui'):
            group = f'{type_}_scripts'
            for ep in dist.entry_points.select(group=group):
                cls._ensure_safe_name(ep.name)
                if ep.name in names:
                    script_text = koskari_script_source(
                        spec,
                        KOSKARI_SCRIPTS_GROUP,
                        ep.name)
                else:
                    script_text = cls.template % {
                        'spec': spec,
                        'group': group,
                        'name': ep.name,
                    }
                yield from cls._get_script_args(
                    type_, ep.name, header, script_text)


class KoskariWindowsScriptWriter(
        KoskariScriptWriter,
        scripts.WindowsScriptWriter):
    """
    koskari_scripts stub generation with Windows script extensions
    """


class KoskariWindowsExecutableLauncherWriter(
        KoskariScriptWriter,
        scripts.WindowsExecutableLauncherWriter):
    """
    koskari_scripts stub generation with Windows exe launchers
    """


def ensure_koskari_script_writer():
    """
    Patch setuptools ScriptWriter to emit koskari_scripts stubs

    Idempotent. Replaces stock ScriptWriter classes in setuptools._scripts
    with koskari-aware mixins, including Windows launcher variants.
    """

    global _SCRIPT_WRITER_PATCHED

    if _SCRIPT_WRITER_PATCHED:
        return

    scripts.ScriptWriter = KoskariScriptWriter
    scripts.WindowsScriptWriter = KoskariWindowsScriptWriter
    scripts.WindowsExecutableLauncherWriter = \
        KoskariWindowsExecutableLauncherWriter

    _SCRIPT_WRITER_PATCHED = True


# The end.
