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

Install koskari_scripts commands as pre-built stub files via install_scripts.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import sys
import textwrap

from setuptools import _scripts as scripts
from setuptools._importlib import metadata as setuptools_metadata
from setuptools.command import install_scripts as install_scripts_module
from setuptools.command.editable_wheel import editable_wheel
from setuptools.warnings import SetuptoolsWarning

from ...launcher import KOSKARI_SCRIPTS_GROUP
from .validate import entry_points_group_lines


__all__ = (
    'KoskariEditableWheel',
    'KoskariInstallScripts',
    'iter_koskari_script_args',
    'koskari_script_source',
)


def koskari_script_source(spec, group, name):
    src = f"""
    # KOSKARI-SCRIPT-ENTRY: {spec!r},{group!r},{name!r}
    from sys import argv, exit
    from koskari.launcher import run_resolved_script

    program = argv[0]
    if program.endswith('script.pyw'):
        argv[0] = program[:-len('script.pyw')]
    elif program.endswith('.exe'):
        argv[0] = program[:-len('.exe')]

    exit(run_resolved_script({spec!r}, {name!r}))
    """
    return textwrap.dedent(src).lstrip()


def _metadata_distribution(dist):
    """
    Return an importlib.metadata Distribution for script generation

    :param dist: setuptools Distribution or metadata Distribution
    :return: metadata Distribution with entry_points.select
    """

    if hasattr(dist, 'entry_points') and hasattr(dist.entry_points, 'select'):
        return dist

    if hasattr(dist, 'egg_info'):
        return setuptools_metadata.Distribution.at(dist.egg_info)

    SetuptoolsWarning.emit("Unsupported distribution encountered.")
    return dist


def iter_koskari_script_args(dist, header=None, executable=None):
    """
    Yield name and contents tuples for koskari_scripts entry points

    Uses stock ScriptWriter helpers for shebang and platform naming.

    :param dist: setuptools or metadata Distribution
    :param header: optional script header; defaults to ScriptWriter header
    :param executable: optional interpreter for the shebang line
    :yield: (script_name, script_contents) pairs
    """

    mdist = _metadata_distribution(dist)
    eps = mdist.entry_points.select(group=KOSKARI_SCRIPTS_GROUP)
    if not eps:
        return

    writer = scripts.ScriptWriter.best()
    if header is None:
        header = writer.get_header(executable=executable)
    spec = f'{mdist.name}=={mdist.version}'

    for ep in eps:
        writer._ensure_safe_name(ep.name)
        script_text = koskari_script_source(
            spec,
            KOSKARI_SCRIPTS_GROUP,
            ep.name)
        yield from writer._get_script_args(
            'console', ep.name, header, script_text)


class KoskariInstallScripts(install_scripts_module.install_scripts):
    """
    Install koskari_scripts stubs before the no_ep early return

    Wheel builds set no_ep True to skip console_scripts generation; koskari
    stubs are written as ordinary script files into .data/scripts/ instead.
    """

    def run(self):
        self.run_command('egg_info')
        if self.distribution.scripts:
            install_scripts_module.orig.install_scripts.run(self)
        else:
            self.outfiles = []
        self._install_koskari_scripts()
        if self.no_ep:
            return
        self._install_ep_scripts()

    def _install_koskari_scripts(self):
        ei_cmd = self.get_finalized_command('egg_info')
        dist = setuptools_metadata.Distribution.at(path=ei_cmd.egg_info)
        if not list(dist.entry_points.select(group=KOSKARI_SCRIPTS_GROUP)):
            return

        bs_cmd = self.get_finalized_command('build_scripts')
        exec_param = getattr(bs_cmd, 'executable', None)
        if exec_param == sys.executable:
            exec_param = [exec_param]

        header = scripts.ScriptWriter.best().get_header(executable=exec_param)
        for args in iter_koskari_script_args(dist, header=header):
            self.write_script(*args)


class KoskariEditableWheel(editable_wheel):
    """
    Run install_scripts for koskari_scripts during PEP 660 editable builds

    editable_wheel only installs scripts when distribution.has_scripts() is
    true, which checks the classic scripts= list rather than koskari_scripts
    entry points.
    """

    def _run_install(self, category):
        if category == 'scripts':
            if entry_points_group_lines(
                    self.distribution.entry_points,
                    KOSKARI_SCRIPTS_GROUP):
                self.run_command('install_scripts')
                return
        super()._run_install(category)


# The end.
