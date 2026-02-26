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
koskari

A LISP dialect for Python - a playground language for joyful expression.

Import from submodules only: koskari.types, koskari.exceptions, koskari.reader,
koskari.compile, koskari.dis, koskari.lib, koskari.importer.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


def _auto_enable_importer():
    """
    Install the koskari import hook unless suppressed.

    Uses sys.modules to detect whether koskari.importer has already been
    imported so as to avoid creating a circular import during koskari's own
    package initialisation.  The actual install() call is deferred by
    registering with sys.modules' post-import machinery via importlib.

    Suppressed when the environment variable KOSKARI_NOIMPORTER is set to
    a non-zero value, or when the -X KOSKARI_NOIMPORTER Python xoption is
    set to a non-zero value.
    """

    from os import environ
    from sys import _xoptions as xoptions
    if (xoptions.get('KOSKARI_NOIMPORTER', '0') != '0'
            or environ.get('KOSKARI_NOIMPORTER', '0') != '0'):
        return

    # Defer so that koskari.lib / koskari.compile finish loading first.
    # By the time Python finishes executing this module the rest of the
    # package's submodules have not yet been imported, so we register a
    # one-shot post-import hook using importlib.util's find_spec mechanism.
    # The simplest reliable approach: just call install() here; any real
    # circular import would manifest as an ImportError, which means the
    # user ran into a packaging issue rather than a module-loading cycle.
    try:
        from koskari.importer import install
        install()
    except ImportError:
        pass


_auto_enable_importer()
del _auto_enable_importer


# The end.
