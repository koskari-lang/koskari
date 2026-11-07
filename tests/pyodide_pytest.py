# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.pyodide_pytest

Runs pytest against the installed Pyodide wheel. cibuildwheel runs its
test command from a temporary directory so that the wheel, not the
source tree, is what the tests import. A test selection written
relative to the repository root cannot be found from there, so this
runner takes the root as its first argument and resolves every relative
selection against it before handing the arguments to pytest. With no
selection it runs the whole tests directory

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import sys
from pathlib import Path

import pytest


def resolve_selection(root, args):
    """
    The pytest arguments with every relative selection taken from root.
    An argument is a selection when its path part, before any ``::``
    node id, names a file or directory under root; anything else, an
    option or an option's value, is passed through as written

    :param root: the repository root
    :param args: selections and options as given on the command line
    :return: the arguments for pytest
    """

    if not args:
        args = ['tests']

    resolved = []
    for arg in args:
        path_part = arg.split('::', 1)[0]
        if (not arg.startswith('-')
                and not Path(path_part).is_absolute()
                and (root / path_part).exists()):
            resolved.append(str(root / arg))
        else:
            resolved.append(arg)
    return resolved


def main(argv):
    root = Path(argv[1]).resolve()
    return pytest.main(resolve_selection(root, argv[2:]))


if __name__ == '__main__':
    sys.exit(main(sys.argv))


# The end.
