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

    Uses the top-level ``koskari_importer`` package so ``koskari.compile`` and
    ``koskari.lib`` are not loaded as a side effect of ``import koskari``.

    Suppressed when the environment variable KOSKARI_NOIMPORTER is set to
    a non-zero value, or when the -X KOSKARI_NOIMPORTER Python xoption is
    set to a non-zero value.
    """

    from os import environ
    from sys import _xoptions as xoptions
    if (xoptions.get('KOSKARI_NOIMPORTER', '0') != '0'
            or environ.get('KOSKARI_NOIMPORTER', '0') != '0'):
        return

    from koskari_importer import install
    install()


_auto_enable_importer()
del _auto_enable_importer


# The end.
