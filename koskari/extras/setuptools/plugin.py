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

The setuptools.finalize_distribution_options hook for koskari_scripts.

Setuptools runs this hook for every Distribution built in an environment
where koskari is installed, so it stays small and touches nothing outside
the one distribution it is handed. The command classes and script writers
live in commands, imported only once a distribution turns out to declare
koskari_scripts.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from .metadata import (KOSKARI_SCRIPTS_GROUP, augment_koskari_packages,
                       entry_points_group_lines, validate_koskari_scripts,
                       warn_koskari_scripts_metadata)


__all__ = (
    'configure_koskari_scripts',
)


# the command classes and script writers keep their old names here but
# resolve through __getattr__ on first use; their home is commands
_LAZY = frozenset((
    'KoskariEditableWheel',
    'KoskariInstallScripts',
    'iter_koskari_script_args',
    'koskari_script_source',
))


def __getattr__(name):
    """
    The command classes and script writers keep their old home here but
    load on first use, so importing the hook loads no setuptools internals
    """

    if name in _LAZY:
        from . import commands
        return getattr(commands, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def configure_koskari_scripts(dist):
    """
    Register the koskari_scripts command classes on a distribution that
    declares koskari_scripts, and leave every other distribution alone

    Registered as setuptools.finalize_distribution_options. Setuptools runs
    that from Distribution.__init__, before setup.cfg or pyproject.toml are
    parsed, so entry points declared only there are not visible yet. A
    distribution with none visible gets a second look after its own
    config parse; see _check_after_config_parse.

    :param dist: setuptools Distribution being finalized
    """

    koskari_lines = entry_points_group_lines(
        getattr(dist, 'entry_points', None), KOSKARI_SCRIPTS_GROUP)
    if koskari_lines:
        _configure(dist)
    else:
        _check_after_config_parse(dist)


def _configure(dist):
    if getattr(dist, '_koskari_scripts_configured', False):
        return

    validate_koskari_scripts(dist)
    augment_koskari_packages(dist)
    warn_koskari_scripts_metadata(dist)

    from .commands import command_classes

    cmdclass = dict(getattr(dist, 'cmdclass', None) or {})
    cmdclass.update(command_classes())
    dist.cmdclass = cmdclass
    dist._koskari_scripts_configured = True


def _check_after_config_parse(dist):
    """
    Arrange a second look at this one distribution once its config files
    are parsed, when a declarative koskari_scripts group becomes visible

    The wrapper is bound on the instance, so nothing is changed for any
    other distribution in the process and no state outlives this one.
    """

    orig = getattr(dist, 'parse_config_files', None)
    if orig is None or getattr(orig, '_koskari_checks', False):
        return

    def parse_config_files(filenames=None, ignore_option_errors=False):
        result = orig(filenames, ignore_option_errors=ignore_option_errors)
        if entry_points_group_lines(
                getattr(dist, 'entry_points', None), KOSKARI_SCRIPTS_GROUP):
            _configure(dist)
        return result

    parse_config_files._koskari_checks = True
    dist.parse_config_files = parse_config_files


# The end.
