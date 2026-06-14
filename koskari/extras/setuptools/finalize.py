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

Setuptools finalize hook for koskari_scripts packaging.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from setuptools.dist import Distribution

from ...launcher import KOSKARI_SCRIPTS_GROUP
from .packages import augment_koskari_packages
from .scripts import KoskariEditableWheel, KoskariInstallScripts
from .validate import (entry_points_group_lines, validate_koskari_scripts,
                       warn_koskari_scripts_metadata)


__all__ = (
    'configure_koskari_scripts',
)


_PARSE_CONFIG_PATCHED = False


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
        _PARSE_CONFIG_PATCHED = True
        return

    def parse_config_files(
            self,
            filenames=None,
            ignore_option_errors=False):

        orig(self, filenames, ignore_option_errors=ignore_option_errors)
        configure_koskari_scripts(self)

    parse_config_files._koskari_wrapped = True
    Distribution.parse_config_files = parse_config_files
    _PARSE_CONFIG_PATCHED = True


# The end.
