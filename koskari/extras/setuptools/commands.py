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
koskari.extras.setuptools.commands

The setuptools command classes and script writers behind koskari_scripts.
Imported only for a distribution that declares koskari_scripts: this is
where the setuptools internals are touched, so a build that never uses
koskari_scripts never loads them. See plugin for the finalize hook.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys
import textwrap
import warnings

from setuptools.command import install_scripts as install_scripts_module

from .metadata import KOSKARI_SCRIPTS_GROUP, entry_points_group_lines


__all__ = (
    'KoskariEditableWheel',
    'KoskariInstallScripts',
    'iter_koskari_script_args',
    'koskari_script_source',
)


# ScriptWriter moved from the easy_install command module into a private
# setuptools module; take whichever this setuptools has, quietly.
try:
    from setuptools._scripts import ScriptWriter
except ImportError:  # pragma: no cover - depends on the setuptools installed
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        from setuptools.command.easy_install import ScriptWriter

try:
    from setuptools._importlib import metadata as importlib_metadata
except ImportError:  # pragma: no cover - depends on the setuptools installed
    from importlib import metadata as importlib_metadata

try:
    from setuptools.command.editable_wheel import editable_wheel
except ImportError:  # pragma: no cover - depends on the setuptools installed
    editable_wheel = None


def koskari_script_source(spec, group, name):
    # Generated stub includes a grep-friendly marker with dist spec and name.
    src = f"""
    # KOSKARI-SCRIPT-ENTRY: {spec!r},{group!r},{name!r}
    from sys import argv, exit
    from koskari.launcher import run_resolved_script

    program = argv[0]
    # Match setuptools console_scripts: strip Windows launcher suffixes so
    # argv[0] is the installed command basename, not script.pyw or foo.exe.
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

    # install_scripts runs after egg_info; read the on-disk dist-info so
    # entry_points match what pip will see (name, version, entry_points.txt).
    if hasattr(dist, 'egg_info'):
        return importlib_metadata.Distribution.at(dist.egg_info)

    warnings.warn("Unsupported distribution encountered.", RuntimeWarning)
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

    writer = ScriptWriter.best()
    if header is None:
        header = writer.get_header(executable=executable)
    spec = f'{mdist.name}=={mdist.version}'

    for ep in eps:
        writer._ensure_safe_name(ep.name)
        script_text = koskari_script_source(
            spec,
            KOSKARI_SCRIPTS_GROUP,
            ep.name)
        # Reuse setuptools ScriptWriter for shebang, .exe naming, and safety
        # checks; body calls run_resolved_script with baked-in dist spec.
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
        # Honor legacy scripts= when present; koskari_scripts are separate.
        if self.distribution.scripts:
            install_scripts_module.orig.install_scripts.run(self)
        else:
            self.outfiles = []
        # Wheel builds set no_ep and return before _install_ep_scripts, but
        # koskari stubs must still land in .data/scripts/ -- write them first.
        self._install_koskari_scripts()
        if self.no_ep:
            return
        self._install_ep_scripts()

    def _install_koskari_scripts(self):
        ei_cmd = self.get_finalized_command('egg_info')
        # Stub embeds distname==version; use finalized egg_info metadata, not
        # the in-flight Distribution attrs, so name/version match dist-info.
        dist = importlib_metadata.Distribution.at(ei_cmd.egg_info)
        if not list(dist.entry_points.select(group=KOSKARI_SCRIPTS_GROUP)):
            return

        bs_cmd = self.get_finalized_command('build_scripts')
        exec_param = getattr(bs_cmd, 'executable', None)
        # build_scripts stores a bare path; ScriptWriter wants a one-element
        # list when the shebang targets the current interpreter.
        if exec_param == sys.executable:
            exec_param = [exec_param]

        header = ScriptWriter.best().get_header(executable=exec_param)
        for args in iter_koskari_script_args(dist, header=header):
            self.write_script(*args)


if editable_wheel is not None:

    class KoskariEditableWheel(editable_wheel):
        """
        Run install_scripts for koskari_scripts during PEP 660 editable builds

        editable_wheel only installs scripts when distribution.has_scripts()
        is true, which checks the classic scripts= list rather than
        koskari_scripts entry points.
        """

        def _run_install(self, category):
            if category == 'scripts':
                # editable_wheel only calls install_scripts when
                # has_scripts() (classic scripts= list) is true;
                # koskari_scripts are entry points.
                if entry_points_group_lines(
                        self.distribution.entry_points,
                        KOSKARI_SCRIPTS_GROUP):
                    self.run_command('install_scripts')
                    return
            super()._run_install(category)

else:  # pragma: no cover - depends on the setuptools installed
    KoskariEditableWheel = None


def command_classes():
    """
    The command class overrides a koskari_scripts distribution needs,
    keyed by command name; editable_wheel is present only when this
    setuptools has that command
    """

    classes = {'install_scripts': KoskariInstallScripts}
    if KoskariEditableWheel is not None:
        classes['editable_wheel'] = KoskariEditableWheel
    return classes


# The end.
