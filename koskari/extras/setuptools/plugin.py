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
koskari.extras.setuptools.plugin

Setuptools integration for koskari_scripts: stub install commands and the
finalize_distribution_options hook.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import sys
import textwrap

from setuptools import _scripts as scripts
from setuptools._importlib import metadata as setuptools_metadata
from setuptools.command import install_scripts as install_scripts_module
from setuptools.command.editable_wheel import editable_wheel
from setuptools.dist import Distribution
from setuptools.warnings import SetuptoolsWarning

from .metadata import (KOSKARI_SCRIPTS_GROUP, augment_koskari_packages,
                       entry_points_group_lines, validate_koskari_scripts,
                       warn_koskari_scripts_metadata)


__all__ = (
    'KoskariEditableWheel',
    'KoskariInstallScripts',
    'configure_koskari_scripts',
    'iter_koskari_script_args',
    'koskari_script_source',
)


_PARSE_CONFIG_PATCHED = False


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
        # koskari stubs must still land in .data/scripts/ — write them first.
        self._install_koskari_scripts()
        if self.no_ep:
            return
        self._install_ep_scripts()

    def _install_koskari_scripts(self):
        ei_cmd = self.get_finalized_command('egg_info')
        # Stub embeds distname==version; use finalized egg_info metadata, not
        # the in-flight Distribution attrs, so name/version match dist-info.
        dist = setuptools_metadata.Distribution.at(path=ei_cmd.egg_info)
        if not list(dist.entry_points.select(group=KOSKARI_SCRIPTS_GROUP)):
            return

        bs_cmd = self.get_finalized_command('build_scripts')
        exec_param = getattr(bs_cmd, 'executable', None)
        # build_scripts stores a bare path; ScriptWriter wants a one-element
        # list when the shebang targets the current interpreter.
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
            # editable_wheel only calls install_scripts when has_scripts()
            # (classic scripts= list) is true; koskari_scripts are entry points.
            if entry_points_group_lines(
                    self.distribution.entry_points,
                    KOSKARI_SCRIPTS_GROUP):
                self.run_command('install_scripts')
                return
        super()._run_install(category)


def configure_koskari_scripts(dist):
    """
    Validate koskari_scripts metadata and register install_scripts hook

    Registered as setuptools.finalize_distribution_options and also invoked
    after Distribution.parse_config_files so setup.cfg metadata is visible.

    Setuptools calls finalize_distribution_options from Distribution.__init__,
    before setup.cfg is parsed, so entry_points declared only in setup.cfg are
    still None at that point. Wrapping parse_config_files covers that path;
    the finalize hook still handles entry_points passed as setup() kwargs.

    :param dist: setuptools Distribution being finalized
    """

    _patch_parse_config_files()

    koskari_lines = entry_points_group_lines(
        dist.entry_points, KOSKARI_SCRIPTS_GROUP)
    if not koskari_lines:
        return

    validate_koskari_scripts(dist)
    augment_koskari_packages(dist)
    warn_koskari_scripts_metadata(dist)

    cmdclass = dict(getattr(dist, 'cmdclass', None) or {})
    cmdclass['install_scripts'] = KoskariInstallScripts
    # PEP 660 editable installs skip install_scripts unless we override here.
    cmdclass['editable_wheel'] = KoskariEditableWheel
    dist.cmdclass = cmdclass


def _patch_parse_config_files():
    """
    Run configure_koskari_scripts after setup.cfg / pyproject.toml are loaded
    """

    global _PARSE_CONFIG_PATCHED

    if _PARSE_CONFIG_PATCHED:
        return

    orig = Distribution.parse_config_files
    if getattr(orig, '_koskari_wrapped', False):
        # Another copy of koskari (or a prior import) already patched the class.
        _PARSE_CONFIG_PATCHED = True
        return

    def parse_config_files(
            self,
            filenames=None,
            ignore_option_errors=False):

        orig(self, filenames, ignore_option_errors=ignore_option_errors)
        # finalize_distribution_options already ran during Distribution.__init__,
        # before setup.cfg / pyproject.toml were read, so dist.entry_points was
        # still empty then. Re-run after config load for declarative koskari_scripts.
        configure_koskari_scripts(self)

    parse_config_files._koskari_wrapped = True
    # Patch once on the class: every Distribution in this process gets the
    # post-parse_config_files hook after the first finalize hook runs.
    Distribution.parse_config_files = parse_config_files
    _PARSE_CONFIG_PATCHED = True


# The end.
